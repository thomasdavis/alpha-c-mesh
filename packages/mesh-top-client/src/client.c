/* client.rs, ported: the endpoint thread, the control-port framing, the shared updates queue.
 * Error texts are Rust's io::Error Display, since the status line shows them. */
#include "mesh/top_client.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

static uint64_t mono_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static struct timespec deadline_ts(uint64_t at_ms) {
    struct timespec t = {.tv_sec = (time_t)(at_ms / 1000u), .tv_nsec = (long)(at_ms % 1000u) * 1000000L};
    return t;
}

static void cond_init_mono(pthread_cond_t *c) {
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(c, &a);
    pthread_condattr_destroy(&a);
}

/* The first whitespace-separated word of line, into w (empty when none). */
static void first_word(const char *line, char *w, size_t cap) {
    while (*line == ' ' || (*line >= '\t' && *line <= '\r')) line++;
    size_t n = 0;
    while (line[n] && !(line[n] == ' ' || (line[n] >= '\t' && line[n] <= '\r')) && n + 1 < cap) {
        w[n] = line[n];
        n++;
    }
    w[n] = 0;
}

static bool word_in(const char *w, const char *const *set, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(w, set[i]) == 0) return true;
    return false;
}

uint32_t mt_reply_timeout_ms(const char *line) {
    static const char *const slow[] = {"load", "upgrade", "rollback", "unload"};
    static const char *const reset[] = {"reset", "train-reset"};
    static const char *const req[] = {"http", "req", "task"};
    char w[32];
    first_word(line, w, sizeof w);
    if (word_in(w, slow, 4)) return 20000;
    if (word_in(w, reset, 2)) return 15000;
    if (word_in(w, req, 3)) return 8000;
    return MT_IO_TIMEOUT_MS;
}

bool mt_binary_reply(const char *line) {
    static const char *const framed[] = {"snap", "http", "req", "peek", "log", "programs", "catalog", "trace", "view", "render"};
    /* `load` alone is the load report; `load <dir>` loads a program (one line). */
    const char *s = line;
    size_t l = strlen(s);
    while (l && (*s == ' ' || (*s >= '\t' && *s <= '\r'))) s++, l--;
    while (l && (s[l - 1] == ' ' || (s[l - 1] >= '\t' && s[l - 1] <= '\r'))) l--;
    if (l == 4 && memcmp(s, "load", 4) == 0) return true;
    char w[32];
    first_word(line, w, sizeof w);
    return word_in(w, framed, sizeof framed / sizeof *framed);
}

/* ---- the updates queue ---- */

struct mt_updates {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    mt_update *q;
    size_t head, n, cap;
};

mt_updates *mt_updates_new(void) {
    mt_updates *q = xmalloc(sizeof *q);
    memset(q, 0, sizeof *q);
    pthread_mutex_init(&q->mu, NULL);
    cond_init_mono(&q->cv);
    return q;
}

void mt_update_free(mt_update *u) {
    free(u->text);
    free(u->data);
    if (u->snap.bytes) mt_snapshot_free(&u->snap);
    memset(u, 0, sizeof *u);
}

void mt_updates_free(mt_updates *q) {
    if (!q) return;
    for (size_t i = 0; i < q->n; i++) mt_update_free(&q->q[(q->head + i) % q->cap]);
    free(q->q);
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->cv);
    free(q);
}

void mt_updates_push(mt_updates *q, mt_update u) {
    pthread_mutex_lock(&q->mu);
    if (q->n == q->cap) {
        size_t cap = q->cap ? q->cap * 2 : 64;
        mt_update *nq = xmalloc(cap * sizeof *nq);
        for (size_t i = 0; i < q->n; i++) nq[i] = q->q[(q->head + i) % q->cap];
        free(q->q);
        q->q = nq;
        q->cap = cap;
        q->head = 0;
    }
    q->q[(q->head + q->n) % q->cap] = u;
    q->n++;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mu);
}

bool mt_updates_pop(mt_updates *q, uint32_t timeout_ms, mt_update *out) {
    pthread_mutex_lock(&q->mu);
    struct timespec dl = deadline_ts(mono_ms() + timeout_ms);
    while (q->n == 0 && timeout_ms) {
        if (pthread_cond_timedwait(&q->cv, &q->mu, &dl) == ETIMEDOUT) break;
    }
    bool got = q->n > 0;
    if (got) {
        *out = q->q[q->head];
        q->head = (q->head + 1) % q->cap;
        q->n--;
    }
    pthread_mutex_unlock(&q->mu);
    return got;
}

/* ---- the endpoint ---- */

struct mt_endpoint {
    size_t idx;
    char *addr;
    mt_updates *up;
    atomic_size_t pending;
    atomic_uint_fast64_t interval;
    pthread_t th;
    pthread_mutex_t mu; /* guards cmds, closed, fd */
    pthread_cond_t cv;
    char **cmds;
    size_t head, n, cap;
    bool closed;
    int fd; /* the live connection, so free can interrupt a request */
};

/* Rust's io::Error Display for an OS error. */
static char *os_err(int e) {
    char b[160];
    snprintf(b, sizeof b, "%s (os error %d)", strerror(e), e);
    return xstrdup(b);
}

typedef enum { RECV_LINE, RECV_TIMEOUT, RECV_CLOSED } recv_res;

/* rx.recv_timeout: a command line, a timeout, or the endpoint being dropped. */
static recv_res recv_cmd(mt_endpoint *e, uint64_t until_ms, char **line) {
    pthread_mutex_lock(&e->mu);
    struct timespec dl = deadline_ts(until_ms);
    for (;;) {
        if (e->n) {
            *line = e->cmds[e->head];
            e->head = (e->head + 1) % e->cap;
            e->n--;
            pthread_mutex_unlock(&e->mu);
            return RECV_LINE;
        }
        if (e->closed) break;
        if (mono_ms() >= until_ms || pthread_cond_timedwait(&e->cv, &e->mu, &dl) == ETIMEDOUT) {
            if (e->n) continue;
            pthread_mutex_unlock(&e->mu);
            return e->closed ? RECV_CLOSED : RECV_TIMEOUT;
        }
    }
    pthread_mutex_unlock(&e->mu);
    return RECV_CLOSED;
}

static bool is_closed(mt_endpoint *e) {
    pthread_mutex_lock(&e->mu);
    bool c = e->closed;
    pthread_mutex_unlock(&e->mu);
    return c;
}

/* `host:port` or `[v6]:port` resolved and connected with a 1 s timeout; NULL or the error. */
static char *dial(const char *addr, int *out) {
    const char *colon = strrchr(addr, ':');
    if (!colon) return xstrdup("invalid socket address");
    char host[256];
    const char *h = addr;
    size_t hl = (size_t)(colon - addr);
    if (hl >= 2 && h[0] == '[' && h[hl - 1] == ']') h++, hl -= 2;
    if (hl >= sizeof host) return xstrdup("invalid socket address");
    memcpy(host, h, hl);
    host[hl] = 0;
    char *end;
    unsigned long port = strtoul(colon + 1, &end, 10);
    if (!colon[1] || *end || port > 65535) return xstrdup("invalid port value");
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_socktype = SOCK_STREAM;
    int g = getaddrinfo(host, colon + 1, &hints, &res);
    if (g != 0) {
        char b[256];
        snprintf(b, sizeof b, "failed to lookup address information: %s", gai_strerror(g));
        return xstrdup(b);
    }
    if (!res) return xstrdup("address resolves to nothing");
    int fd = socket(res->ai_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        int er = errno;
        freeaddrinfo(res);
        return os_err(er);
    }
    int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int r = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0 && errno != EINPROGRESS) {
        int er = errno;
        close(fd);
        return os_err(er);
    }
    if (r != 0) {
        struct pollfd p = {.fd = fd, .events = POLLOUT};
        int k;
        do k = poll(&p, 1, 1000);
        while (k < 0 && errno == EINTR);
        if (k == 0) {
            close(fd);
            return xstrdup("connection timed out");
        }
        int so = 0;
        socklen_t sl = sizeof so;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &so, &sl);
        if (k < 0 || so) {
            int er = k < 0 ? errno : so;
            close(fd);
            return os_err(er);
        }
    }
    fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    struct timeval tv = {.tv_sec = MT_IO_TIMEOUT_MS / 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    *out = fd;
    return NULL;
}

/* A connection with BufReader's buffer. */
typedef struct {
    int fd;
    uint8_t buf[8192];
    size_t at, have;
} conn;

/* Fills the buffer; NULL, or the error. */
static char *fill(conn *c) {
    for (;;) {
        ssize_t k = recv(c->fd, c->buf, sizeof c->buf, 0);
        if (k < 0 && errno == EINTR) continue;
        if (k < 0) return os_err(errno);
        c->at = 0;
        c->have = (size_t)k;
        return NULL;
    }
}

static char *read_exact(conn *c, uint8_t *b, size_t n) {
    while (n) {
        if (c->at == c->have) {
            char *e = fill(c);
            if (e) return e;
            if (!c->have) return xstrdup("failed to fill whole buffer");
        }
        size_t k = c->have - c->at < n ? c->have - c->at : n;
        memcpy(b, c->buf + c->at, k);
        c->at += k;
        b += k;
        n -= k;
    }
    return NULL;
}

/* read_line onto prefix, then trim_end; *out, or the error. */
static char *text_line(conn *c, const char *prefix, char **out) {
    size_t cap = 256, n = strlen(prefix), got = 0;
    char *s = xmalloc(cap + n);
    memcpy(s, prefix, n);
    cap += n;
    for (;;) {
        if (c->at == c->have) {
            char *e = fill(c);
            if (e) {
                free(s);
                return e;
            }
            if (!c->have) break;
        }
        uint8_t ch = c->buf[c->at++];
        if (n + 1 >= cap) {
            cap *= 2;
            char *t = realloc(s, cap);
            if (!t) abort();
            s = t;
        }
        s[n++] = (char)ch;
        got++;
        if (ch == '\n') break;
    }
    if (!got) {
        free(s);
        return xstrdup("connection closed");
    }
    while (n && (s[n - 1] == ' ' || (s[n - 1] >= '\t' && s[n - 1] <= '\r'))) n--;
    s[n] = 0;
    *out = s;
    return NULL;
}

static char *send_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR) continue;
        if (k < 0) return os_err(errno);
        if (k == 0) return xstrdup("failed to write whole buffer");
        p += k;
        n -= (size_t)k;
    }
    return NULL;
}

typedef struct {
    bool bytes;
    uint8_t *data;
    size_t len;
} reply;

static char *request(conn *c, const char *line, reply *r) {
    uint32_t ms = mt_reply_timeout_ms(line);
    struct timeval tv = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    size_t ll = strlen(line);
    char *msg = xmalloc(ll + 1);
    memcpy(msg, line, ll);
    msg[ll] = '\n';
    char *e = send_all(c->fd, msg, ll + 1);
    free(msg);
    if (e) return e;
    char *t;
    if (!mt_binary_reply(line)) {
        if ((e = text_line(c, "", &t))) return e;
        r->bytes = false;
        r->data = (uint8_t *)t;
        r->len = strlen(t);
        return NULL;
    }
    uint8_t len[4];
    if ((e = read_exact(c, len, 4))) return e;
    /* A refused framed request answers with a text `err ...` line; its first four bytes would
     * decode to a ~544 MB length, so they are unambiguous. */
    if (memcmp(len, "err ", 4) == 0) {
        if ((e = text_line(c, "err ", &t))) return e;
        r->bytes = false;
        r->data = (uint8_t *)t;
        r->len = strlen(t);
        return NULL;
    }
    size_t n = (size_t)len[0] | (size_t)len[1] << 8 | (size_t)len[2] << 16 | (size_t)len[3] << 24;
    if (n > MT_MAX_REPLY) {
        char b[96];
        snprintf(b, sizeof b, "reply length %zu exceeds %zu", n, MT_MAX_REPLY);
        return xstrdup(b);
    }
    uint8_t *buf = xmalloc(n);
    if ((e = read_exact(c, buf, n))) {
        free(buf);
        return e;
    }
    r->bytes = true;
    r->data = buf;
    r->len = n;
    return NULL;
}

static mt_update reply_update(size_t idx, char *line, reply r) {
    mt_update u;
    memset(&u, 0, sizeof u);
    u.kind = MT_UPDATE_REPLY;
    u.machine = idx;
    u.text = line;
    u.bytes = r.bytes;
    u.data = r.data;
    u.len = r.len;
    return u;
}

static mt_update text_update(mt_update_kind k, size_t idx, char *text) {
    mt_update u;
    memset(&u, 0, sizeof u);
    u.kind = k;
    u.machine = idx;
    u.text = text;
    return u;
}

static void set_fd(mt_endpoint *e, int fd) {
    pthread_mutex_lock(&e->mu);
    e->fd = fd;
    pthread_mutex_unlock(&e->mu);
}

/* Sleeps ms unless the endpoint is dropped first; false when it was. */
static bool nap(mt_endpoint *e, uint64_t ms) {
    pthread_mutex_lock(&e->mu);
    struct timespec dl = deadline_ts(mono_ms() + ms);
    while (!e->closed)
        if (pthread_cond_timedwait(&e->cv, &e->mu, &dl) == ETIMEDOUT) break;
    bool open = !e->closed;
    pthread_mutex_unlock(&e->mu);
    return open;
}

static void *run(void *arg) {
    mt_endpoint *e = arg;
    uint64_t backoff = MT_BACKOFF_MIN_MS;
    conn *c = xmalloc(sizeof *c);
    for (;;) {
        if (is_closed(e)) break;
        int fd = -1;
        char *err = dial(e->addr, &fd);
        if (err) {
            mt_updates_push(e->up, text_update(MT_UPDATE_OFFLINE, e->idx, err));
            /* Commands issued while offline fail at once instead of piling up. */
            uint64_t deadline = mono_ms() + backoff;
            bool dropped = false;
            for (;;) {
                char *line;
                recv_res r = recv_cmd(e, deadline, &line);
                if (r == RECV_TIMEOUT) break;
                if (r == RECV_CLOSED) {
                    dropped = true;
                    break;
                }
                atomic_fetch_sub(&e->pending, 1);
                reply rp = {false, (uint8_t *)xstrdup("err offline"), 11};
                mt_updates_push(e->up, reply_update(e->idx, line, rp));
            }
            if (dropped) break;
            backoff = backoff * 2 < MT_BACKOFF_MAX_MS ? backoff * 2 : MT_BACKOFF_MAX_MS;
            continue;
        }
        c->fd = fd;
        c->at = c->have = 0;
        set_fd(e, fd);
        backoff = MT_BACKOFF_MIN_MS;
        mt_updates_push(e->up, text_update(MT_UPDATE_ONLINE, e->idx, NULL));
        uint64_t next_snap = mono_ms();
        bool dropped = false;
        for (;;) {
            char *line;
            recv_res r = recv_cmd(e, next_snap, &line);
            if (r == RECV_CLOSED) {
                dropped = true;
                break;
            }
            if (r == RECV_LINE) {
                reply rp;
                err = request(c, line, &rp);
                atomic_fetch_sub(&e->pending, 1);
                if (!err) {
                    /* Doom input shows on the next frame, not 66 ms later. */
                    if (strncmp(line, "input ", 6) == 0 || strncmp(line, "key ", 4) == 0) next_snap = mono_ms();
                    mt_updates_push(e->up, reply_update(e->idx, line, rp));
                    continue;
                }
                size_t el = strlen(err);
                char *t = xmalloc(el + 5);
                memcpy(t, "err ", 4);
                memcpy(t + 4, err, el + 1);
                rp.bytes = false;
                rp.data = (uint8_t *)t;
                rp.len = el + 4;
                mt_updates_push(e->up, reply_update(e->idx, line, rp));
                break;
            }
            reply rp;
            err = request(c, "snap", &rp);
            next_snap = mono_ms() + atomic_load(&e->interval);
            if (err) break;
            if (rp.bytes) {
                mt_snapshot s;
                char *perr = NULL;
                if (mt_snapshot_parse(rp.data, rp.len, &s, &perr)) mt_updates_push(e->up, mt_update_snap(e->idx, s));
                else mt_updates_push(e->up, text_update(MT_UPDATE_SNAP_ERROR, e->idx, perr));
                free(rp.data);
            } else {
                mt_updates_push(e->up, text_update(MT_UPDATE_SNAP_ERROR, e->idx, (char *)rp.data));
            }
        }
        set_fd(e, -1);
        close(fd);
        if (dropped) break;
        mt_updates_push(e->up, text_update(MT_UPDATE_OFFLINE, e->idx, err));
        if (!nap(e, backoff)) break;
    }
    free(c);
    return NULL;
}

mt_endpoint *mt_endpoint_spawn(size_t idx, const char *addr, mt_updates *updates, uint64_t interval_ms) {
    mt_endpoint *e = xmalloc(sizeof *e);
    memset(e, 0, sizeof *e);
    e->idx = idx;
    e->addr = xstrdup(addr);
    e->up = updates;
    e->fd = -1;
    atomic_init(&e->pending, 0);
    atomic_init(&e->interval, interval_ms);
    pthread_mutex_init(&e->mu, NULL);
    cond_init_mono(&e->cv);
    if (pthread_create(&e->th, NULL, run, e) != 0) abort();
    return e;
}

void mt_endpoint_send(mt_endpoint *e, const char *line) {
    atomic_fetch_add(&e->pending, 1);
    pthread_mutex_lock(&e->mu);
    if (e->closed) {
        atomic_fetch_sub(&e->pending, 1);
        pthread_mutex_unlock(&e->mu);
        return;
    }
    if (e->n == e->cap) {
        size_t cap = e->cap ? e->cap * 2 : 16;
        char **nq = xmalloc(cap * sizeof *nq);
        for (size_t i = 0; i < e->n; i++) nq[i] = e->cmds[(e->head + i) % e->cap];
        free(e->cmds);
        e->cmds = nq;
        e->cap = cap;
        e->head = 0;
    }
    e->cmds[(e->head + e->n) % e->cap] = xstrdup(line);
    e->n++;
    pthread_cond_signal(&e->cv);
    pthread_mutex_unlock(&e->mu);
}

size_t mt_endpoint_pending(const mt_endpoint *e) { return atomic_load(&((mt_endpoint *)e)->pending); }

void mt_endpoint_set_interval(mt_endpoint *e, uint64_t ms) { atomic_store(&e->interval, ms); }

uint64_t mt_endpoint_interval(const mt_endpoint *e) { return atomic_load(&((mt_endpoint *)e)->interval); }

const char *mt_endpoint_addr(const mt_endpoint *e) { return e->addr; }

void mt_endpoint_free(mt_endpoint *e) {
    if (!e) return;
    pthread_mutex_lock(&e->mu);
    e->closed = true;
    if (e->fd >= 0) shutdown(e->fd, SHUT_RDWR);
    pthread_cond_broadcast(&e->cv);
    pthread_mutex_unlock(&e->mu);
    pthread_join(e->th, NULL);
    for (size_t i = 0; i < e->n; i++) free(e->cmds[(e->head + i) % e->cap]);
    free(e->cmds);
    free(e->addr);
    pthread_mutex_destroy(&e->mu);
    pthread_cond_destroy(&e->cv);
    free(e);
}
