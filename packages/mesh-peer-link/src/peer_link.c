/* peer.rs: connect, handshake, acceptor, dialer, reader, writer, Link. */
#include "mesh/peer_link.h"
#include "mesh/abi/exchange.h"
#include "mesh/abi/host_mload.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/text.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* "host:port" into its parts (the last ':' splits). */
static bool split_addr(const char *addr, char **host, char **port) {
    const char *c = strrchr(addr, ':');
    if (!c) return false;
    *host = mt_strndup(addr, (size_t)(c - addr));
    *port = mt_strdup(c + 1);
    return true;
}

int mpn_connect(const char *addr, uint32_t timeout_ms, char **err) {
    char *host, *port;
    if (!split_addr(addr, &host, &port)) {
        *err = mt_fmt("connect %s: no port", addr);
        return -1;
    }
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM}, *res = NULL;
    int g = getaddrinfo(host, port, &hints, &res);
    free(host), free(port);
    if (g != 0 || !res) {
        *err = mt_fmt("connect %s: address resolves to nothing", addr);
        return -1;
    }
    int fd = socket(res->ai_family, SOCK_STREAM, 0);
    if (fd < 0) {
        *err = mt_fmt("connect %s: %s", addr, strerror(errno));
        freeaddrinfo(res);
        return -1;
    }
    int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int r = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0 && errno != EINPROGRESS) goto fail;
    if (r != 0) {
        struct pollfd p = {.fd = fd, .events = POLLOUT};
        int k = poll(&p, 1, (int)timeout_ms);
        if (k == 0) {
            errno = ETIMEDOUT;
            goto fail;
        }
        int soerr = 0;
        socklen_t sl = sizeof soerr;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (k < 0 || soerr) {
            errno = soerr ? soerr : errno;
            goto fail;
        }
    }
    fcntl(fd, F_SETFL, fl);
    return fd;
fail:
    *err = mt_fmt("connect %s: %s", addr, strerror(errno));
    close(fd);
    return -1;
}

int mpn_listen(const char *addr, char **err) {
    char *host, *port;
    if (!split_addr(addr, &host, &port)) {
        *err = mt_fmt("cannot bind %s: no port", addr);
        return -1;
    }
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM, .ai_flags = AI_PASSIVE}, *res = NULL;
    int g = getaddrinfo(host, port, &hints, &res);
    free(host), free(port);
    if (g != 0 || !res) {
        *err = mt_fmt("cannot bind %s: address resolves to nothing", addr);
        return -1;
    }
    int fd = socket(res->ai_family, SOCK_STREAM, 0), one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (fd < 0 || bind(fd, res->ai_addr, res->ai_addrlen) != 0 || listen(fd, 16) != 0) {
        *err = mt_fmt("cannot bind %s: %s", addr, strerror(errno));
        if (fd >= 0) close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);
    return fd;
}

static bool write_all(int fd, const uint8_t *b, size_t n) {
    while (n) {
        ssize_t w = send(fd, b, n, MSG_NOSIGNAL);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        b += w, n -= (size_t)w;
    }
    return true;
}

typedef struct {
    int fd;
    uint32_t dead_ms;
} sockread;

static bool sock_read(void *ctx, uint8_t *buf, size_t n, char **err) {
    sockread *s = ctx;
    while (n) {
        ssize_t r = recv(s->fd, buf, n, 0);
        if (r > 0) {
            buf += r, n -= (size_t)r;
            continue;
        }
        if (r < 0 && errno == EINTR) continue;
        if (r == 0) *err = mt_strdup("closed");
        else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT) *err = mt_fmt("silent for %u ms", s->dead_ms);
        else *err = mt_strdup(strerror(errno));
        return false;
    }
    return true;
}

static void set_timeouts(int fd, uint32_t ms) {
    struct timeval tv = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

bool mpn_handshake(int fd, uint32_t me, uint32_t n, const mpk_caps *caps, bool has_expect, uint32_t expect, uint32_t dead_ms, mpk_join *out, char **err) {
    set_timeouts(fd, dead_ms);
    mpk_join mine = {me, MESH_PEER_VERSION, n, *caps};
    mpw_bytes f = mpk_join_encode(&mine);
    bool ok = write_all(fd, f.b, f.n);
    mpw_bytes_free(&f);
    if (!ok) {
        *err = mt_fmt("JOIN: %s", strerror(errno));
        return false;
    }
    sockread s = {fd, dead_ms};
    uint8_t ty;
    char *e = NULL;
    if (!sock_read(&s, &ty, 1, &e)) {
        *err = mt_fmt("no JOIN: %s", e);
        free(e);
        return false;
    }
    if (ty != MESH_F_JOIN) {
        *err = mt_fmt("first frame type %u is not F_JOIN (a peer-link version below 4?)", ty);
        return false;
    }
    uint8_t b[MESH_JOIN_BYTES];
    if (!sock_read(&s, b, sizeof b, &e)) {
        *err = mt_fmt("short JOIN: %s", e);
        free(e);
        return false;
    }
    mpk_join j = mpk_join_decode(b);
    if (!mpk_join_check(&j, me, n, has_expect, expect, err)) return false;
    *out = j;
    return true;
}

/* ---- framing */

static bool rd(mpn_read_fn read, void *ctx, mpw_bytes *f, size_t n, char **err) {
    if (n == 0) return true;
    uint8_t *b = mt_alloc(n);
    bool ok = read(ctx, b, n, err);
    if (ok) mpw_put(f, b, n);
    free(b);
    return ok;
}

bool mpn_read_frame(mpn_read_fn read, void *ctx, mpw_bytes *f, char **err) {
    *f = (mpw_bytes){0};
    uint8_t ty;
    if (!read(ctx, &ty, 1, err)) return false;
    mpw_put(f, &ty, 1);
    size_t at = 1;
    uint32_t n;
#define HEAD(len)                                  \
    do {                                           \
        if (!rd(read, ctx, f, (len), err)) goto bad; \
    } while (0)
#define BODY(len) HEAD(len)
    switch (ty) {
    case MESH_F_HELLO: HEAD(MESH_HELLO_BYTES); break;
    case MESH_F_ROUND: HEAD(MESH_ROUND_HEAD + MESH_EX_REGION_BYTES); break;
    case MESH_F_BEAT:
        HEAD(MESH_BEAT_HEAD);
        n = mpw_u32_at(f->b, at + MESH_BEAT_HEAD - 4);
        if (n > MESH_BEAT_REASON_MAX) {
            *err = mt_fmt("F_BEAT reason of %u bytes", n);
            goto bad;
        }
        BODY(n);
        HEAD(4);
        n = mpw_u32_at(f->b, f->n - 4);
        if (n > MESH_BEAT_MOVES_MAX) {
            *err = mt_fmt("F_BEAT with %u move outcomes", n);
            goto bad;
        }
        BODY((size_t)n * MESH_BEAT_MOVE_BYTES);
        break;
    case MESH_F_VOTE_REQ: HEAD(MESH_VOTE_REQ_BYTES); break;
    case MESH_F_VOTE: HEAD(MESH_VOTE_BYTES); break;
    case MESH_F_STATE:
        HEAD(MESH_STATE_HEAD);
        n = mpw_u32_at(f->b, at + 8);
        if (n > MESH_F_STATE_MAX) {
            *err = mt_fmt("F_STATE of %u bytes > F_STATE_MAX", n);
            goto bad;
        }
        BODY(n);
        break;
    case MESH_F_STATE_ACK:
    case MESH_F_COMMIT: HEAD(MESH_ACK_BYTES); break;
    case MESH_F_CTL:
        HEAD(MESH_CTL_HEAD);
        n = mpw_u32_at(f->b, at + 4);
        if (n > MESH_F_CTL_MAX) {
            *err = mt_fmt("F_CTL of %u bytes", n);
            goto bad;
        }
        BODY(n);
        break;
    case MESH_F_CTL_REPLY:
        HEAD(MESH_CTL_REPLY_HEAD);
        n = mpw_u32_at(f->b, at + 8);
        if (n > MESH_F_CTL_REPLY_MAX) {
            *err = mt_fmt("F_CTL_REPLY of %u bytes", n);
            goto bad;
        }
        BODY(n);
        break;
    case MESH_F_IMAGE_REQ:
        HEAD(MESH_IMAGE_REQ_HEAD);
        n = mpw_u32_at(f->b, at + MESH_IMAGE_REQ_HEAD - 4);
        if (n > 64) {
            *err = mt_fmt("F_IMAGE_REQ dir of %u bytes", n);
            goto bad;
        }
        BODY(n);
        break;
    case MESH_F_IMAGE: {
        HEAD(MESH_IMAGE_HEAD);
        size_t total = (size_t)mpw_u32_at(f->b, at + 16) + mpw_u32_at(f->b, at + 20) + mpw_u32_at(f->b, at + 24);
        if (total > MESH_F_LOAD_MAX) {
            *err = mt_fmt("F_IMAGE of %zu bytes > F_LOAD_MAX", total);
            goto bad;
        }
        BODY(total);
        break;
    }
    case MESH_F_PLACE: {
        HEAD(MPW_PLACE_HEAD);
        size_t rest;
        if (!mpw_place_rest_len(f->b + at, &rest, err)) goto bad;
        BODY(rest);
        break;
    }
    case MESH_F_FRAME: {
        HEAD(MPW_FRAME_HEAD);
        size_t rest;
        if (!mpw_frame_rest_len(f->b + at, &rest, err)) goto bad;
        BODY(rest);
        break;
    }
    case MESH_F_INPUT: {
        HEAD(MPW_INPUT_HEAD);
        size_t rest;
        if (!mpw_input_len(f->b + at, &rest, err)) goto bad;
        BODY(rest);
        break;
    }
    case MESH_F_PERF:
        HEAD(4);
        n = mpw_u32_at(f->b, at);
        if (n > MESH_F_PERF_MAX) {
            *err = mt_fmt("F_PERF of %u bytes", n);
            goto bad;
        }
        BODY(n);
        break;
    case MESH_F_VIEW_REQ: HEAD(MESH_VIEW_REQ_BYTES); break;
    case MESH_F_VIEW:
        HEAD(MESH_VIEW_HEAD);
        n = mpw_u32_at(f->b, at + 20);
        if (n > MESH_F_VIEW_CHUNK) {
            *err = mt_fmt("F_VIEW chunk of %u bytes", n);
            goto bad;
        }
        BODY(n);
        break;
    case MESH_F_LOADREP: HEAD(MESH_LOADREP_BYTES); break;
    default:
        if (ty >= MESH_F_JOB_AVAIL && ty <= MESH_F_RESULT) {
            HEAD(MESH_JOB_HEAD);
            n = mpw_u32_at(f->b, at);
            if (n > MESH_F_JOB_MAX) {
                *err = mt_fmt("job frame type %u of %u bytes > F_JOB_MAX", ty, n);
                goto bad;
            }
            BODY(n);
        } else if (ty >= MESH_F_BLOB_PUT && ty <= MESH_F_BLOB) {
            HEAD(MESH_JOB_HEAD);
            n = mpw_u32_at(f->b, at);
            if (n > MESH_F_BLOB_MAX) {
                *err = mt_fmt("blob frame type %u of %u bytes > F_BLOB_MAX", ty, n);
                goto bad;
            }
            BODY(n);
        } else {
            *err = mt_fmt("bad frame type %u", ty);
            goto bad;
        }
    }
    return true;
bad:
    mpw_bytes_free(f);
    return false;
#undef HEAD
#undef BODY
}

/* ---- a link */

typedef struct {
    uint8_t *b;
    size_t n;
} qframe;

typedef struct {
    qframe *v;
    size_t head, n, cap;
} queue;

struct mpn_link {
    int fd;
    uint32_t from, dead_ms;
    uint64_t lid;
    mpn_sink sink;
    atomic_bool alive;
    atomic_bool *dialer_alive;
    pthread_mutex_t wlock; /* the writer lock: one frame at a time on the socket */
    pthread_mutex_t qmu;
    pthread_cond_t qcv;
    queue ctl, bulk;
    pthread_t reader, writer;
    bool closed_once;
};

static bool q_push(queue *q, const uint8_t *f, size_t n) {
    if (q->n == q->cap) return false;
    size_t i = (q->head + q->n) % q->cap;
    q->v[i].b = mt_alloc(n ? n : 1);
    memcpy(q->v[i].b, f, n);
    q->v[i].n = n;
    q->n++;
    return true;
}

static bool q_pop(queue *q, qframe *out) {
    if (!q->n) return false;
    *out = q->v[q->head];
    q->head = (q->head + 1) % q->cap;
    q->n--;
    return true;
}

static void q_free(queue *q) {
    qframe f;
    while (q_pop(q, &f)) free(f.b);
    free(q->v);
}

static void mark_dead(mpn_link *l) {
    atomic_store(&l->alive, false);
    if (l->dialer_alive) atomic_store(l->dialer_alive, false);
    shutdown(l->fd, SHUT_RDWR);
    pthread_mutex_lock(&l->qmu);
    pthread_cond_broadcast(&l->qcv);
    pthread_mutex_unlock(&l->qmu);
}

static bool link_write(mpn_link *l, const uint8_t *f, size_t n) {
    pthread_mutex_lock(&l->wlock);
    bool ok = write_all(l->fd, f, n);
    pthread_mutex_unlock(&l->wlock);
    if (ok && l->sink.count && n) l->sink.count(l->sink.ctx, l->from, true, f[0], n);
    return ok;
}

static void *reader_main(void *arg) {
    mpn_link *l = arg;
    sockread s = {l->fd, l->dead_ms};
    char *why = NULL;
    for (;;) {
        mpw_bytes f;
        if (!mpn_read_frame(sock_read, &s, &f, &why)) break;
        if (l->sink.count) l->sink.count(l->sink.ctx, l->from, false, f.b[0], f.n);
        l->sink.frame(l->sink.ctx, l->from, l->lid, f);
    }
    mark_dead(l);
    l->sink.closed(l->sink.ctx, l->from, l->lid, why ? why : "closed");
    free(why);
    return NULL;
}

/* Writes the member's queued frames until the link dies: every control frame queued before each bulk
 * frame, so a blob in flight delays a beat or a lease by one bulk frame at most. */
static void *writer_main(void *arg) {
    mpn_link *l = arg;
    for (;;) {
        qframe f;
        pthread_mutex_lock(&l->qmu);
        while (!l->ctl.n && !l->bulk.n && atomic_load(&l->alive)) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 100 * 1000000L;
            if (ts.tv_nsec >= 1000000000L) ts.tv_sec++, ts.tv_nsec -= 1000000000L;
            pthread_cond_timedwait(&l->qcv, &l->qmu, &ts);
        }
        if (!atomic_load(&l->alive)) {
            pthread_mutex_unlock(&l->qmu);
            return NULL;
        }
        bool got = q_pop(&l->ctl, &f) || q_pop(&l->bulk, &f);
        pthread_mutex_unlock(&l->qmu);
        if (!got) continue;
        bool ok = link_write(l, f.b, f.n);
        free(f.b);
        if (!ok) {
            mark_dead(l);
            return NULL;
        }
    }
}

mpn_link *mpn_link_start(int fd, uint32_t from, uint64_t lid, uint32_t dead_ms, const mpn_sink *sink, atomic_bool *alive) {
    mpn_link *l = mt_alloc(sizeof *l);
    l->fd = fd;
    l->from = from;
    l->lid = lid;
    l->dead_ms = dead_ms;
    l->sink = *sink;
    l->dialer_alive = alive;
    atomic_init(&l->alive, true);
    pthread_mutex_init(&l->wlock, NULL);
    pthread_mutex_init(&l->qmu, NULL);
    pthread_cond_init(&l->qcv, NULL);
    l->ctl = (queue){mt_alloc(MPN_LINK_QUEUE * sizeof(qframe)), 0, 0, MPN_LINK_QUEUE};
    l->bulk = (queue){mt_alloc(MPN_BULK_QUEUE * sizeof(qframe)), 0, 0, MPN_BULK_QUEUE};
    set_timeouts(fd, dead_ms);
    pthread_create(&l->reader, NULL, reader_main, l);
    pthread_create(&l->writer, NULL, writer_main, l);
    return l;
}

uint32_t mpn_link_from(const mpn_link *l) { return l->from; }
uint64_t mpn_link_lid(const mpn_link *l) { return l->lid; }
bool mpn_link_alive(const mpn_link *l) { return atomic_load(&l->alive); }

static bool enqueue(mpn_link *l, queue *q, const uint8_t *f, size_t n) {
    if (!atomic_load(&l->alive)) return false;
    pthread_mutex_lock(&l->qmu);
    bool ok = q_push(q, f, n);
    pthread_cond_signal(&l->qcv);
    pthread_mutex_unlock(&l->qmu);
    return ok;
}

bool mpn_send(mpn_link *l, const uint8_t *f, size_t n) { return enqueue(l, &l->ctl, f, n); }
bool mpn_send_bulk(mpn_link *l, const uint8_t *f, size_t n) { return enqueue(l, &l->bulk, f, n); }

bool mpn_write_now(mpn_link *l, const uint8_t *f, size_t n) {
    if (!atomic_load(&l->alive)) return false;
    if (!link_write(l, f, n)) {
        mark_dead(l);
        return false;
    }
    return true;
}

void mpn_link_close(mpn_link *l) { mark_dead(l); }

void mpn_link_free(mpn_link *l) {
    if (!l) return;
    mark_dead(l);
    pthread_join(l->reader, NULL);
    pthread_join(l->writer, NULL);
    close(l->fd);
    q_free(&l->ctl);
    q_free(&l->bulk);
    pthread_mutex_destroy(&l->wlock);
    pthread_mutex_destroy(&l->qmu);
    pthread_cond_destroy(&l->qcv);
    free(l);
}

/* ---- the acceptor and dialers */

struct mpn_acceptor {
    int fd;
    uint32_t me, n, dead_ms;
    mpk_caps caps;
    mpn_owner owner;
    pthread_t th;
    atomic_bool stop;
};

typedef struct {
    mpn_acceptor *a;
    int fd;
} hs_job;

static void *handshake_main(void *arg) {
    hs_job *j = arg;
    mpk_join join;
    char *err = NULL;
    if (mpn_handshake(j->fd, j->a->me, j->a->n, &j->a->caps, false, 0, j->a->dead_ms, &join, &err)) j->a->owner.linked(j->a->owner.ctx, j->fd, &join, NULL);
    else {
        if (j->a->owner.log) {
            char *line = mt_fmt("peer: link from a lower system refused: %s", err);
            j->a->owner.log(j->a->owner.ctx, line);
            free(line);
        }
        free(err);
        close(j->fd);
    }
    free(j);
    return NULL;
}

static void *acceptor_main(void *arg) {
    mpn_acceptor *a = arg;
    while (!atomic_load(&a->stop)) {
        struct pollfd p = {.fd = a->fd, .events = POLLIN};
        if (poll(&p, 1, 100) <= 0) continue;
        int c = accept(a->fd, NULL, NULL);
        if (c < 0) continue;
        hs_job *j = mt_alloc(sizeof *j);
        j->a = a, j->fd = c;
        pthread_t t;
        pthread_create(&t, NULL, handshake_main, j);
        pthread_detach(t);
    }
    return NULL;
}

mpn_acceptor *mpn_acceptor_start(int listen_fd, uint32_t me, uint32_t n, const mpk_caps *caps, uint32_t dead_ms, const mpn_owner *owner) {
    mpn_acceptor *a = mt_alloc(sizeof *a);
    *a = (mpn_acceptor){.fd = listen_fd, .me = me, .n = n, .dead_ms = dead_ms, .caps = *caps, .owner = *owner};
    atomic_init(&a->stop, false);
    pthread_create(&a->th, NULL, acceptor_main, a);
    return a;
}

void mpn_acceptor_stop(mpn_acceptor *a) {
    if (!a) return;
    atomic_store(&a->stop, true);
    pthread_join(a->th, NULL);
    close(a->fd);
    free(a);
}

struct mpn_dialer {
    uint32_t j, me, n, dead_ms;
    char *addr;
    mpk_caps caps;
    mpn_owner owner;
    pthread_t th;
    atomic_bool stop, alive;
};

static void sleep_ms(uint32_t ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static void *dialer_main(void *arg) {
    mpn_dialer *d = arg;
    uint32_t backoff = 100;
    char *said = mt_strdup("");
    while (!atomic_load(&d->stop)) {
        char *err = NULL;
        int fd = mpn_connect(d->addr, d->dead_ms, &err);
        mpk_join join;
        if (fd >= 0 && !mpn_handshake(fd, d->me, d->n, &d->caps, true, d->j, d->dead_ms, &join, &err)) {
            close(fd);
            fd = -1;
        }
        if (fd >= 0) {
            backoff = 100;
            mt_set(&said, "");
            atomic_store(&d->alive, true);
            d->owner.linked(d->owner.ctx, fd, &join, &d->alive);
            while (atomic_load(&d->alive) && !atomic_load(&d->stop)) sleep_ms(50);
            continue;
        }
        /* Refusals are loud once (a version or size mismatch), not every retry. */
        if (strcmp(err, said) != 0 && !mt_starts(err, "connect ") && d->owner.log) {
            char *line = mt_fmt("peer: link to system %u (%s): %s", d->j, d->addr, err);
            d->owner.log(d->owner.ctx, line);
            free(line);
        }
        mt_set(&said, err);
        free(err);
        for (uint32_t t = 0; t < backoff && !atomic_load(&d->stop); t += 20) sleep_ms(20);
        backoff = backoff * 2 > 1000 ? 1000 : backoff * 2;
    }
    free(said);
    return NULL;
}

mpn_dialer *mpn_dialer_start(uint32_t j, const char *addr, uint32_t me, uint32_t n, const mpk_caps *caps, uint32_t dead_ms, const mpn_owner *owner) {
    mpn_dialer *d = mt_alloc(sizeof *d);
    *d = (mpn_dialer){.j = j, .me = me, .n = n, .dead_ms = dead_ms, .addr = mt_strdup(addr), .caps = *caps, .owner = *owner};
    atomic_init(&d->stop, false);
    atomic_init(&d->alive, false);
    pthread_create(&d->th, NULL, dialer_main, d);
    return d;
}

void mpn_dialer_stop(mpn_dialer *d) {
    if (!d) return;
    atomic_store(&d->stop, true);
    pthread_join(d->th, NULL);
    free(d->addr);
    free(d);
}
