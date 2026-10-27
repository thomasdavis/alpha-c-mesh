/* The mock's control port: an accepting thread polls a non-blocking listener, each connection
 * gets a thread that reads request lines and answers framed bytes (u32 LE length + bytes) or one
 * text line, exactly as mock.rs's Mock::start_with / serve do. */
#include "mesh/top_mock.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    pthread_t th;
    int fd;
    struct mt_mock *m;
} conn;

struct mt_mock {
    char addr[64];
    mt_mock_world *world;
    bool owned;
    pthread_mutex_t world_lock;
    pthread_mutex_t log_lock;
    char **log;
    size_t nlog, caplog;
    int lfd;
    atomic_bool stop;
    pthread_t acceptor;
    pthread_mutex_t conns_lock;
    conn **conns;
    size_t nconns, capconns;
};

static bool split_addr(const char *addr, struct sockaddr_in *sa) {
    const char *colon = strrchr(addr, ':');
    if (!colon) return false;
    char host[64];
    size_t hl = (size_t)(colon - addr);
    if (hl >= sizeof host) return false;
    memcpy(host, addr, hl);
    host[hl] = 0;
    if (strcmp(host, "localhost") == 0) strcpy(host, "127.0.0.1");
    char *end;
    unsigned long port = strtoul(colon + 1, &end, 10);
    if (*end || port > 65535) return false;
    memset(sa, 0, sizeof *sa);
    sa->sin_family = AF_INET;
    sa->sin_port = htons((uint16_t)port);
    return inet_pton(AF_INET, host, &sa->sin_addr) == 1;
}

static bool send_all(int fd, const void *p, size_t n) {
    const uint8_t *b = p;
    while (n) {
        ssize_t k = send(fd, b, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        b += k;
        n -= (size_t)k;
    }
    return true;
}

static void log_req(struct mt_mock *m, const char *req) {
    pthread_mutex_lock(&m->log_lock);
    if (m->nlog == m->caplog) {
        m->caplog = m->caplog ? m->caplog * 2 : 64;
        m->log = realloc(m->log, m->caplog * sizeof *m->log);
        if (!m->log) abort();
    }
    m->log[m->nlog] = malloc(strlen(req) + 1);
    if (!m->log[m->nlog]) abort();
    strcpy(m->log[m->nlog++], req);
    pthread_mutex_unlock(&m->log_lock);
}

/* The trimmed request, as the log keeps it. */
static char *trimmed(const char *s, size_t l) {
    while (l && (*s == ' ' || (*s >= '\t' && *s <= '\r'))) s++, l--;
    while (l && (s[l - 1] == ' ' || (s[l - 1] >= '\t' && s[l - 1] <= '\r'))) l--;
    char *r = malloc(l + 1);
    if (!r) abort();
    memcpy(r, s, l);
    r[l] = 0;
    return r;
}

static void *serve(void *arg) {
    conn *c = arg;
    struct mt_mock *m = c->m;
    size_t cap = 4096, have = 0;
    char *buf = malloc(cap);
    if (!buf) abort();
    for (;;) {
        char *nl = have ? memchr(buf, '\n', have) : NULL;
        if (!nl) {
            if (have == cap) {
                cap *= 2;
                buf = realloc(buf, cap);
                if (!buf) abort();
            }
            ssize_t k = recv(c->fd, buf + have, cap - have, 0);
            if (k < 0 && errno == EINTR) continue;
            if (k <= 0) break; /* EOF (a partial last line is dropped, as read_line's caller does) */
            have += (size_t)k;
            continue;
        }
        size_t ll = (size_t)(nl - buf) + 1;
        char *req = trimmed(buf, ll);
        memmove(buf, buf + ll, have - ll);
        have -= ll;
        uint8_t *bytes;
        size_t len;
        char *text;
        pthread_mutex_lock(&m->world_lock);
        mt_mock_request(m->world, req, &bytes, &len, &text);
        pthread_mutex_unlock(&m->world_lock);
        log_req(m, req);
        free(req);
        bool ok;
        if (text) {
            size_t tl = strlen(text);
            char *t = malloc(tl + 2);
            if (!t) abort();
            memcpy(t, text, tl);
            t[tl] = '\n';
            ok = send_all(c->fd, t, tl + 1);
            free(t);
            free(text);
        } else {
            uint8_t hdr[4] = {(uint8_t)len, (uint8_t)(len >> 8), (uint8_t)(len >> 16), (uint8_t)(len >> 24)};
            ok = send_all(c->fd, hdr, 4) && send_all(c->fd, bytes, len);
            free(bytes);
        }
        if (!ok) break;
    }
    free(buf);
    return NULL;
}

static void *accept_loop(void *arg) {
    struct mt_mock *m = arg;
    while (!atomic_load(&m->stop)) {
        struct pollfd p = {.fd = m->lfd, .events = POLLIN};
        if (poll(&p, 1, 10) <= 0) continue;
        int fd = accept(m->lfd, NULL, NULL);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL);
        if (fl >= 0) fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);
        conn *c = malloc(sizeof *c);
        if (!c) abort();
        c->fd = fd;
        c->m = m;
        pthread_mutex_lock(&m->conns_lock);
        if (atomic_load(&m->stop)) {
            pthread_mutex_unlock(&m->conns_lock);
            close(fd);
            free(c);
            break;
        }
        if (m->nconns == m->capconns) {
            m->capconns = m->capconns ? m->capconns * 2 : 8;
            m->conns = realloc(m->conns, m->capconns * sizeof *m->conns);
            if (!m->conns) abort();
        }
        if (pthread_create(&c->th, NULL, serve, c) != 0) {
            pthread_mutex_unlock(&m->conns_lock);
            close(fd);
            free(c);
            continue;
        }
        m->conns[m->nconns++] = c;
        pthread_mutex_unlock(&m->conns_lock);
    }
    return NULL;
}

static mt_mock *start(const char *addr, mt_mock_world *w, bool owned) {
    struct sockaddr_in sa;
    if (!split_addr(addr, &sa)) return NULL;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    socklen_t sl = sizeof sa;
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(fd, 64) != 0 ||
        getsockname(fd, (struct sockaddr *)&sa, &sl) != 0) {
        close(fd);
        return NULL;
    }
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    mt_mock *m = calloc(1, sizeof *m);
    if (!m) abort();
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof ip);
    snprintf(m->addr, sizeof m->addr, "%s:%u", ip, (unsigned)ntohs(sa.sin_port));
    m->world = w;
    m->owned = owned;
    m->lfd = fd;
    atomic_init(&m->stop, false);
    pthread_mutex_init(&m->world_lock, NULL);
    pthread_mutex_init(&m->log_lock, NULL);
    pthread_mutex_init(&m->conns_lock, NULL);
    if (pthread_create(&m->acceptor, NULL, accept_loop, m) != 0) {
        close(fd);
        free(m);
        return NULL;
    }
    return m;
}

mt_mock *mt_mock_start(const char *addr, const char *host, uint32_t system) {
    mt_mock_world *w = malloc(sizeof *w);
    if (!w) abort();
    mt_mock_world_init(w, host, system);
    mt_mock *m = start(addr, w, true);
    if (!m) {
        mt_mock_world_free(w);
        free(w);
    }
    return m;
}

mt_mock *mt_mock_start_with(const char *addr, mt_mock_world *w) { return start(addr, w, false); }

const char *mt_mock_addr(const mt_mock *m) { return m->addr; }

mt_mock_world *mt_mock_world_of(mt_mock *m) { return m->world; }

void mt_mock_lock(mt_mock *m) { pthread_mutex_lock(&m->world_lock); }

void mt_mock_unlock(mt_mock *m) { pthread_mutex_unlock(&m->world_lock); }

bool mt_mock_saw(mt_mock *m, const char *req) {
    pthread_mutex_lock(&m->log_lock);
    bool saw = false;
    for (size_t i = 0; i < m->nlog && !saw; i++) saw = strcmp(m->log[i], req) == 0;
    pthread_mutex_unlock(&m->log_lock);
    return saw;
}

void mt_mock_stop(mt_mock *m) {
    if (!m) return;
    atomic_store(&m->stop, true);
    pthread_join(m->acceptor, NULL);
    close(m->lfd);
    pthread_mutex_lock(&m->conns_lock);
    for (size_t i = 0; i < m->nconns; i++) shutdown(m->conns[i]->fd, SHUT_RDWR);
    pthread_mutex_unlock(&m->conns_lock);
    for (size_t i = 0; i < m->nconns; i++) {
        pthread_join(m->conns[i]->th, NULL);
        close(m->conns[i]->fd);
        free(m->conns[i]);
    }
    free(m->conns);
    for (size_t i = 0; i < m->nlog; i++) free(m->log[i]);
    free(m->log);
    if (m->owned) {
        mt_mock_world_free(m->world);
        free(m->world);
    }
    pthread_mutex_destroy(&m->world_lock);
    pthread_mutex_destroy(&m->log_lock);
    pthread_mutex_destroy(&m->conns_lock);
    free(m);
}
