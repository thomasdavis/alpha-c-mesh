/* peer.rs handshake_over_tcp; the framed reader's rules (every frame type, hostile lengths refused);
 * a link's frames both ways, control before bulk, and its close. */
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/abi/place_ops.h"
#include "mesh/check.h"
#include "mesh/peer_link.h"
#include "mesh/mt.h"

#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static mpk_caps caps(const char *host) {
    mpk_caps c = {.cc = 121, .sms = 48, .sms_total = 48, .blocks_per_sm = 24, .gpu = "NVIDIA GB10", .arch = "aarch64", .max_threads = 1024, .max_shared = 101376};
    snprintf(c.host, sizeof c.host, "%s", host);
    return c;
}

typedef struct {
    int fd;
    bool ok;
    mpk_join join;
} side;

static void *accept_side(void *arg) {
    side *s = arg;
    int c = accept(s->fd, NULL, NULL);
    char *err = NULL;
    mpk_caps k = caps("dgx-00");
    s->ok = mpn_handshake(c, 2, 3, &k, false, 0, 3000, &s->join, &err);
    free(err);
    close(c);
    return NULL;
}

static void handshake(void) {
    char *err = NULL;
    int l = mpn_listen("127.0.0.1:0", &err);
    CHECK(l >= 0);
    struct sockaddr_in a;
    socklen_t al = sizeof a;
    getsockname(l, (struct sockaddr *)&a, &al);
    char *addr = mt_fmt("127.0.0.1:%u", ntohs(a.sin_port));
    side s = {.fd = l};
    pthread_t t;
    pthread_create(&t, NULL, accept_side, &s);
    int c = mpn_connect(addr, 3000, &err);
    CHECK(c >= 0);
    mpk_caps k = caps("dialer");
    mpk_join mine;
    CHECK(mpn_handshake(c, 0, 3, &k, true, 2, 3000, &mine, &err));
    pthread_join(t, NULL);
    CHECK(s.ok && mine.system == 2 && s.join.system == 0 && !strcmp(s.join.caps.host, "dialer"));
    close(c);
    close(l);
    /* Nothing listens: a bounded, clear failure. */
    c = mpn_connect(addr, 500, &err);
    CHECK(c < 0 && mt_starts(err, "connect 127.0.0.1:"));
    free(err);
    free(addr);
}

typedef struct {
    const uint8_t *b;
    size_t n, at;
} memread;

static bool mem_read(void *ctx, uint8_t *buf, size_t n, char **err) {
    memread *m = ctx;
    if (m->at + n > m->n) {
        *err = mt_strdup("closed");
        return false;
    }
    memcpy(buf, m->b + m->at, n);
    m->at += n;
    return true;
}

/* Reads one frame back from bytes; the frame must be exactly them. */
static bool reads_back(const mpw_bytes *f) {
    memread m = {f->b, f->n, 0};
    mpw_bytes g;
    char *err = NULL;
    bool ok = mpn_read_frame(mem_read, &m, &g, &err) && g.n == f->n && !memcmp(g.b, f->b, f->n) && m.at == f->n;
    if (!ok) fprintf(stderr, "frame type %u: %s\n", f->b[0], err ? err : "differs");
    free(err);
    mpw_bytes_free(&g);
    return ok;
}

static bool refused(const uint8_t *b, size_t n, const char *needle) {
    memread m = {b, n, 0};
    mpw_bytes g;
    char *err = NULL;
    bool ok = !mpn_read_frame(mem_read, &m, &g, &err) && mt_contains(err, needle);
    if (!ok) fprintf(stderr, "wanted %s, got %s\n", needle, err ? err : "a frame");
    free(err);
    return ok;
}

static void framing(void) {
    mpk_beat_status st = {.reason = "ok"};
    mfc_msg beat = {.kind = MFC_BEAT, .term = 1};
    mpw_bytes f = mpk_beat_frame(&beat, &st);
    CHECK(reads_back(&f));
    /* A hostile reason length: refused before it is read. */
    uint32_t big = MESH_BEAT_REASON_MAX + 1;
    memcpy(f.b + 1 + MESH_BEAT_HEAD - 4, &big, 4);
    CHECK(refused(f.b, f.n, "F_BEAT reason of 161 bytes"));
    mpw_bytes_free(&f);
    mfc_msg m[] = {{.kind = MFC_VOTE_REQ, .term = 2}, {.kind = MFC_VOTE, .term = 2}, {.kind = MFC_STATE, .term = 2}, {.kind = MFC_STATE_ACK}, {.kind = MFC_COMMIT}};
    for (int i = 0; i < 5; i++) {
        f = mpk_msg_frame(&m[i]);
        CHECK(reads_back(&f));
        mpw_bytes_free(&f);
    }
    mpk_hello h = {.group = "train", .order = {0, 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
    f = mpk_hello_frame(1, &h);
    CHECK(reads_back(&f));
    mpw_bytes_free(&f);
    mpw_place p = {.op = MESH_PL_SET, .id = 1, .name = "doom", .mode = mpm_pin(1)};
    f = mpw_place_encode(&p);
    CHECK(reads_back(&f));
    mpw_bytes_free(&f);
    mpw_frame fr = {.name = "doom", .frame = 1, .parts = 1, .data = (uint8_t *)"abcd", .ndata = 4};
    f = mpw_frame_encode(&fr);
    CHECK(reads_back(&f));
    mpw_bytes_free(&f);
    f = mpw_input_encode("doom", (const uint8_t *)"w", 1);
    CHECK(reads_back(&f));
    mpw_bytes_free(&f);
    /* Length-prefixed bodies, and their limits. */
    uint8_t perf[1 + 4 + 3] = {MESH_F_PERF, 3, 0, 0, 0, 'a', 'b', 'c'};
    mpw_bytes pf = {perf, sizeof perf, sizeof perf};
    CHECK(reads_back(&pf));
    uint8_t ctl[1 + MESH_CTL_HEAD] = {MESH_F_CTL, 1, 0, 0, 0, 0xff, 0xff, 0, 0};
    CHECK(refused(ctl, sizeof ctl, "F_CTL of 65535 bytes"));
    uint8_t state[1 + MESH_STATE_HEAD] = {MESH_F_STATE};
    uint32_t huge = MESH_F_STATE_MAX + 1;
    memcpy(state + 1 + 8, &huge, 4);
    CHECK(refused(state, sizeof state, "F_STATE of"));
    uint8_t job[1 + 4] = {MESH_F_LEASE, 0, 0, 0, 0x7f};
    CHECK(refused(job, sizeof job, "job frame type 26"));
    uint8_t bad[] = {99};
    CHECK(refused(bad, 1, "bad frame type 99"));
    uint8_t cut[] = {MESH_F_VOTE, 1, 2};
    CHECK(refused(cut, sizeof cut, "closed"));
}

/* ---- a link both ways over a socket pair */

typedef struct {
    pthread_mutex_t mu;
    uint8_t types[64];
    int n;
    bool closed;
    char why[64];
} got;

static void on_frame(void *ctx, uint32_t from, uint64_t lid, mpw_bytes f) {
    (void)from, (void)lid;
    got *g = ctx;
    pthread_mutex_lock(&g->mu);
    if (g->n < 64) g->types[g->n++] = f.b[0];
    pthread_mutex_unlock(&g->mu);
    mpw_bytes_free(&f);
}

static void on_closed(void *ctx, uint32_t from, uint64_t lid, const char *why) {
    (void)from, (void)lid;
    got *g = ctx;
    pthread_mutex_lock(&g->mu);
    g->closed = true;
    snprintf(g->why, sizeof g->why, "%s", why);
    pthread_mutex_unlock(&g->mu);
}

static void wait_for(got *g, int n) {
    for (int i = 0; i < 400; i++) {
        pthread_mutex_lock(&g->mu);
        bool done = g->n >= n;
        pthread_mutex_unlock(&g->mu);
        if (done) return;
        struct timespec ts = {0, 5000000};
        nanosleep(&ts, NULL);
    }
}

static void links(void) {
    int sv[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    got ga = {.mu = PTHREAD_MUTEX_INITIALIZER}, gb = {.mu = PTHREAD_MUTEX_INITIALIZER};
    mpn_sink sa = {&ga, on_frame, on_closed, NULL}, sb = {&gb, on_frame, on_closed, NULL};
    atomic_bool dialer;
    atomic_init(&dialer, true);
    mpn_link *a = mpn_link_start(sv[0], 1, 1, 3000, &sa, &dialer), *b = mpn_link_start(sv[1], 0, 1, 3000, &sb, NULL);
    mfc_msg v = {.kind = MFC_VOTE, .term = 5};
    mpw_bytes f = mpk_msg_frame(&v);
    uint8_t perf[] = {MESH_F_PERF, 1, 0, 0, 0, 'x'};
    CHECK(mpn_send_bulk(a, perf, sizeof perf));
    CHECK(mpn_send(a, f.b, f.n) && mpn_send(a, f.b, f.n));
    wait_for(&gb, 3);
    CHECK(gb.n == 3);
    CHECK(mpn_write_now(b, f.b, f.n));
    wait_for(&ga, 1);
    CHECK(ga.n == 1 && ga.types[0] == MESH_F_VOTE);
    /* Closing one side: the other's reader sees it, and the dialer's flag goes false. */
    mpn_link_close(b);
    for (int i = 0; i < 400 && !ga.closed; i++) {
        struct timespec ts = {0, 5000000};
        nanosleep(&ts, NULL);
    }
    CHECK(ga.closed && !strcmp(ga.why, "closed") && !atomic_load(&dialer) && !mpn_link_alive(a));
    CHECK(!mpn_send(a, f.b, f.n));
    mpn_link_free(a);
    mpn_link_free(b);
    mpw_bytes_free(&f);
}

int main(void) {
    handshake();
    framing();
    links();
    return mesh_check_done();
}
