/* A fleet on loopback: N relays over real TCP links with a fake daemon behind each (a status block,
 * a mailbox, a reconciler that applies every committed document at once).
 * - they elect one leader;
 * - a change asked for on a follower goes to the leader, commits, reconciles everywhere and is
 *   answered;
 * - a member that stops is seen gone (fleet core, status block, `fleet` text);
 * - the leader stopping elects another among the majority left, which commits changes;
 * - a member that comes back catches up to the committed document;
 * - without a majority a change is refused. */
#include "mesh/check.h"
#include "mesh/abi/host_fleet.h"
#include "mesh/abi/host_members.h"
#include "mesh/abi/host_sched.h"
#include "mesh/abi/reconcile.h"
#include "mesh/peer_relay.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define N 3
#define HOST_BYTES 0x4000
#define MB_BYTES 0x30000

typedef struct {
    uint32_t k;
    pthread_mutex_t mu;
    uint8_t host[HOST_BYTES];
    uint8_t *mb;
    uint64_t t0;
    uint64_t applied;
    uint32_t cordon; /* the committed document's, as the reconciler saw it */
    uint32_t ncommitted;
    char *fleet, *groups, *sched, *recs;
    bool failed;
} fake;

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void sleep_ms(long ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static bool verbose;

static void put(fake *f, size_t off, const void *b, size_t n) {
    if (off + n > HOST_BYTES) return;
    pthread_mutex_lock(&f->mu);
    memcpy(f->host + off, b, n);
    pthread_mutex_unlock(&f->mu);
}
static void host_u32(void *c, size_t off, uint32_t v) { put(c, off, &v, 4); }
static void host_u64(void *c, size_t off, uint64_t v) { put(c, off, &v, 8); }
static void host_str(void *c, size_t off, size_t cap, const char *s) {
    char b[256] = {0};
    if (cap > sizeof b) cap = sizeof b;
    strncpy(b, s, cap - 1);
    put(c, off, b, cap);
}
static void host_bytes(void *c, size_t off, const uint8_t *b, size_t n) { put(c, off, b, n); }
static void host_read(void *c, size_t off, uint8_t *out, size_t n) {
    fake *f = c;
    memset(out, 0, n);
    if (off + n > HOST_BYTES) return;
    pthread_mutex_lock(&f->mu);
    memcpy(out, f->host + off, n);
    pthread_mutex_unlock(&f->mu);
}
static uint32_t get32(fake *f, size_t off) {
    uint32_t v;
    host_read(f, off, (uint8_t *)&v, 4);
    return v;
}
static uint64_t mono_ms(void);
static void sleep_ms(long ms);

/* Waits up to ms for the status block's u32 at off to be v (masked). */
static bool wait_u32(fake *f, size_t off, uint32_t mask, uint32_t v, uint32_t ms) {
    uint64_t end = mono_ms() + ms;
    while ((get32(f, off) & mask) != v && mono_ms() < end) sleep_ms(20);
    return (get32(f, off) & mask) == v;
}

static uint64_t uptime_ms(void *c) { return mono_ms() - ((fake *)c)->t0 + 1; }
static uint64_t mb_get64(void *c, size_t off) {
    uint64_t v = 0;
    if (off + 8 <= MB_BYTES) memcpy(&v, ((fake *)c)->mb + off, 8);
    return v;
}
static uint32_t mb_get32(void *c, size_t off) {
    uint32_t v = 0;
    if (off + 4 <= MB_BYTES) memcpy(&v, ((fake *)c)->mb + off, 4);
    return v;
}
static void mb_read(void *c, size_t off, uint8_t *out, size_t n) {
    memset(out, 0, n);
    if (off + n <= MB_BYTES) memcpy(out, ((fake *)c)->mb + off, n);
}
static void mb_write(void *c, size_t off, const uint8_t *b, size_t n) {
    if (off + n <= MB_BYTES) memcpy(((fake *)c)->mb + off, b, n);
}
static void mb_publish64(void *c, size_t off, uint64_t v) { mb_write(c, off, (const uint8_t *)&v, 8); }
static void lock_feed(void *c, bool on) { (void)c, (void)on; }
static void round_stats(void *c, uint32_t k, uint64_t n, double rtt, double wait) { (void)c, (void)k, (void)n, (void)rtt, (void)wait; }

/* The reconciler: applies a committed document at once. */
static void committed(void *c, const mfd_doc *doc) {
    fake *f = c;
    pthread_mutex_lock(&f->mu);
    f->applied = doc->version;
    f->cordon = doc->cordon;
    f->ncommitted++;
    pthread_mutex_unlock(&f->mu);
}
static void recon(void *c, mpr_recon *out) {
    fake *f = c;
    pthread_mutex_lock(&f->mu);
    *out = (mpr_recon){.applied = f->applied, .status = f->applied ? MESH_RS_SYNC : MESH_RS_NONE, .reason = mt_strdup(f->applied ? "ok" : "")};
    pthread_mutex_unlock(&f->mu);
}
static bool catalog(void *c, const char *dir, mfe_image *out, char **err) {
    (void)c, (void)out;
    *err = mt_fmt("no %s in the catalog", dir);
    return false;
}
static mfl_actual *inventory(void *c, size_t *n) {
    (void)c;
    *n = 0;
    return NULL;
}
static bool image(void *c, const char *dir, const char *name, uint64_t hash, mpr_payload *out, char **err) {
    (void)c, (void)dir, (void)name, (void)hash, (void)out;
    *err = mt_strdup("no images here");
    return false;
}
static bool stop_and_view(void *c, const char *name, uint32_t *abi, mpw_bytes *view, char **err) {
    (void)c, (void)name, (void)abi, (void)view;
    *err = mt_strdup("no views here");
    return false;
}
static void driver_view(void *c, uint32_t up, bool hl, uint32_t l, const mfd_doc *d) { (void)c, (void)up, (void)hl, (void)l, (void)d; }
static void place_frame(void *c, uint32_t from, mpw_bytes f) {
    (void)c, (void)from;
    mpw_bytes_free(&f);
}
static void other_frame(void *c, uint32_t from, uint8_t t, const uint8_t *b, size_t n) { (void)c, (void)from, (void)t, (void)b, (void)n; }
static void jobs_ctl(void *c, const char *line, bool remote, uint32_t from, uint32_t id, mpr_reply reply) {
    (void)c, (void)line, (void)remote, (void)from, (void)id;
    if (reply.fn) reply.fn(reply.ctx, false, "no jobs here");
}
static void jobs_view(void *c, uint64_t t, bool hl, uint32_t l, uint32_t a, uint32_t k) { (void)c, (void)t, (void)hl, (void)l, (void)a, (void)k; }
static void jobs_sched(void *c, const msm_member *ms, size_t n, const mfd_doc *d, const msm_perfs *p, const msw_weights *w) {
    (void)c, (void)ms, (void)n, (void)d, (void)p, (void)w;
}
static void texts(void *c, const char *fleet, const char *groups) {
    fake *f = c;
    pthread_mutex_lock(&f->mu);
    mt_set(&f->fleet, fleet);
    mt_set(&f->groups, groups);
    pthread_mutex_unlock(&f->mu);
}
static void sched_texts(void *c, const char *all, const msp_explains *ex, const char *dry, const char *recs) {
    (void)ex, (void)dry;
    fake *f = c;
    pthread_mutex_lock(&f->mu);
    mt_set(&f->sched, all);
    mt_set(&f->recs, recs);
    pthread_mutex_unlock(&f->mu);
}
static void peer_state(void *c, uint32_t s) { (void)c, (void)s; }
static void net_count(void *c, uint32_t k, bool tx, uint8_t t, size_t n) { (void)c, (void)k, (void)tx, (void)t, (void)n; }
static void logline(void *c, bool error, const char *line) {
    if (verbose) fprintf(stderr, "[%u]%s %s\n", ((fake *)c)->k, error ? " E" : "", line);
}
static void fail_stop(void *c, const char *why) {
    ((fake *)c)->failed = true;
    fprintf(stderr, "[%u] fail-stop: %s\n", ((fake *)c)->k, why);
}

static mpr_host host_of(fake *f) {
    return (mpr_host){f,           host_u32,   host_u64,     host_str,    host_bytes,    host_read,   uptime_ms, mb_get64,    mb_get32,   mb_read,
                      mb_write,    mb_publish64, lock_feed,  round_stats, committed,     recon,       catalog,   inventory,   image,      stop_and_view,
                      driver_view, place_frame, other_frame, other_frame, jobs_ctl,      jobs_view,   jobs_sched, texts,      sched_texts, peer_state,
                      net_count,   logline,     fail_stop};
}

/* ---- the fleet */

static fake fakes[N];
static mpr_relay *relays[N];
static char *conf;

static uint16_t free_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t l = sizeof a;
    bind(fd, (struct sockaddr *)&a, sizeof a);
    getsockname(fd, (struct sockaddr *)&a, &l);
    close(fd);
    return ntohs(a.sin_port);
}

static void start(uint32_t k) {
    fake *f = &fakes[k];
    if (!f->mb) {
        pthread_mutex_init(&f->mu, NULL);
        f->mb = calloc(1, MB_BYTES);
    }
    pthread_mutex_lock(&f->mu);
    memset(f->host, 0, sizeof f->host);
    f->applied = 0, f->cordon = 0, f->ncommitted = 0;
    f->k = k;
    f->t0 = mono_ms();
    pthread_mutex_unlock(&f->mu);
    mcc_topology t;
    char *err = NULL;
    if (!mcc_topology_parse(conf, k, &t, &err)) {
        fprintf(stderr, "%s\n", err);
        abort();
    }
    mpk_caps caps = {.cc = 0, .backend = 0};
    snprintf(caps.gpu, sizeof caps.gpu, "none");
    snprintf(caps.arch, sizeof caps.arch, "x86_64");
    snprintf(caps.host, sizeof caps.host, "loop-%u", k);
    mpr_host h = host_of(f);
    relays[k] = mpr_start(&t, &caps, NULL, &h, &err);
    if (!relays[k]) {
        fprintf(stderr, "start %u: %s\n", k, err);
        abort();
    }
    mcc_topology_free(&t);
}

static void stop(uint32_t k) {
    mpr_stop(relays[k]);
    relays[k] = NULL;
}

/* The leader every running relay agrees on (and that leads), or -1. */
static int agreed_leader(void) {
    int l = -1;
    for (uint32_t k = 0; k < N; k++) {
        if (!relays[k]) continue;
        uint32_t x;
        if (!mpr_leader(relays[k], &x)) return -1;
        if (l >= 0 && (uint32_t)l != x) return -1;
        l = (int)x;
    }
    if (l < 0 || !relays[l] || mpr_role(relays[l]) != MESH_ROLE_LEADER) return -1;
    return l;
}

static int wait_leader(uint32_t ms) {
    uint64_t end = mono_ms() + ms;
    int l;
    while ((l = agreed_leader()) < 0 && mono_ms() < end) sleep_ms(20);
    return l;
}

static bool wait_alive(uint32_t k, uint32_t mask, uint32_t ms) {
    uint64_t end = mono_ms() + ms;
    while (mono_ms() < end) {
        if (mpr_alive(relays[k]) == mask) return true;
        sleep_ms(20);
    }
    return false;
}

/* ---- a change and its answer */

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    bool done, ok;
    char *text;
} answer;

static void on_answer(void *ctx, bool ok, const char *text) {
    answer *a = ctx;
    pthread_mutex_lock(&a->mu);
    a->done = true, a->ok = ok;
    a->text = mt_strdup(text);
    pthread_cond_signal(&a->cv);
    pthread_mutex_unlock(&a->mu);
}

/* Asks member k for the change; true when answered ok within ms (*text: the answer). */
static bool change(uint32_t k, const char *line, uint32_t ms, char **text) {
    answer a = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER};
    mpr_change(relays[k], line, (mpr_reply){on_answer, &a});
    uint64_t end = mono_ms() + ms;
    pthread_mutex_lock(&a.mu);
    while (!a.done && mono_ms() < end) {
        pthread_mutex_unlock(&a.mu);
        sleep_ms(10);
        pthread_mutex_lock(&a.mu);
    }
    bool done = a.done, ok = a.ok;
    pthread_mutex_unlock(&a.mu);
    if (!done) {
        fprintf(stderr, "change %s on %u: no answer in %u ms\n", line, k, ms);
        abort();
    }
    if (verbose) fprintf(stderr, "change %s on %u: %s %s\n", line, k, ok ? "ok" : "refused", a.text);
    if (text) *text = a.text;
    else free(a.text);
    return ok;
}

static bool wait_applied(uint32_t k, uint64_t version, uint32_t ms) {
    uint64_t end = mono_ms() + ms;
    while (mono_ms() < end) {
        pthread_mutex_lock(&fakes[k].mu);
        uint64_t a = fakes[k].applied;
        pthread_mutex_unlock(&fakes[k].mu);
        if (a >= version) return true;
        sleep_ms(20);
    }
    return false;
}

static char *fleet_text(uint32_t k) {
    pthread_mutex_lock(&fakes[k].mu);
    char *s = mt_strdup(fakes[k].fleet ? fakes[k].fleet : "");
    pthread_mutex_unlock(&fakes[k].mu);
    return s;
}

static bool wait_fleet_text(uint32_t k, const char *needle, uint32_t ms) {
    uint64_t end = mono_ms() + ms;
    while (mono_ms() < end) {
        char *s = fleet_text(k);
        bool yes = strstr(s, needle) != NULL;
        free(s);
        if (yes) return true;
        sleep_ms(20);
    }
    char *s = fleet_text(k);
    fprintf(stderr, "member %u's fleet text has no \"%s\":\n%s", k, needle, s);
    free(s);
    return false;
}

static bool wait_recs(uint32_t k, const char *needle, uint32_t ms) {
    uint64_t end = mono_ms() + ms;
    while (mono_ms() < end) {
        pthread_mutex_lock(&fakes[k].mu);
        bool yes = fakes[k].recs && strstr(fakes[k].recs, needle);
        pthread_mutex_unlock(&fakes[k].mu);
        if (yes) return true;
        sleep_ms(20);
    }
    return false;
}

int main(int argc, char **argv) {
    verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    mt_buf b = {0};
    mt_printf(&b, "systems = %d\nping_ms = 50\ndead_ms = 600\n", N);
    for (int k = 0; k < N; k++) mt_printf(&b, "%d.peer = 127.0.0.1:%u\n", k, free_port());
    conf = mt_take(&b);
    for (uint32_t k = 0; k < N; k++) start(k);

    /* One leader, every member up. */
    int l = wait_leader(10000);
    CHECK(l >= 0);
    for (uint32_t k = 0; k < N; k++) CHECK(wait_alive(k, (1u << N) - 1, 5000));
    /* The leader's first document commits and reconciles everywhere. */
    for (uint32_t k = 0; k < N; k++) CHECK(wait_applied(k, 1, 5000));
    uint32_t f1 = (uint32_t)(l + 1) % N, f2 = (uint32_t)(l + 2) % N;
    char lead_word[32];
    snprintf(lead_word, sizeof lead_word, "loop-%d leader", l);
    CHECK(wait_fleet_text(f1, lead_word, 3000));
    CHECK(wait_u32(&fakes[f1], MESH_S_LEADER, ~0u, (uint32_t)l, 3000));
    CHECK(wait_u32(&fakes[l], MESH_S_ROLE, ~0u, MESH_ROLE_LEADER, 3000));
    CHECK(wait_u32(&fakes[f1], MESH_S_UP_MASK, ~0u, (1u << N) - 1, 3000));
    CHECK(wait_u32(&fakes[f1], MESH_S_FLEET_FLAGS, MESH_FF_QUORUM, MESH_FF_QUORUM, 3000));

    /* A change asked for on a follower: forwarded, committed, reconciled, answered. */
    char *text = NULL;
    CHECK(change(f1, "sched cordon 2", 15000, &text));
    free(text);
    uint64_t v = mpr_version(relays[l]);
    for (uint32_t k = 0; k < N; k++) {
        CHECK(wait_applied(k, v, 5000));
        pthread_mutex_lock(&fakes[k].mu);
        CHECK(fakes[k].cordon == 1u << 2);
        pthread_mutex_unlock(&fakes[k].mu);
    }
    /* sched_publish (every second) puts the cordon in the status block and the view. */
    uint64_t end = mono_ms() + 3000;
    while (get32(&fakes[f2], MESH_S_CORDON) != 1u << 2 && mono_ms() < end) sleep_ms(20);
    CHECK(get32(&fakes[f2], MESH_S_CORDON) == 1u << 2);
    CHECK(wait_recs(f2, "cordon=2", 3000));
    /* A change that is not one: refused by the leader, the answer says why. */
    CHECK(!change(f2, "sched frobnicate", 15000, &text));
    CHECK(text && strstr(text, "not a change"));
    free(text);

    /* A follower stops: the others see it gone. */
    stop(f2);
    uint32_t two = (1u << l) | (1u << f1);
    CHECK(wait_alive((uint32_t)l, two, 5000));
    CHECK(wait_alive(f1, two, 5000));
    char gone[32];
    snprintf(gone, sizeof gone, "member %u down", f2);
    CHECK(wait_fleet_text(f1, gone, 3000));
    CHECK(wait_u32(&fakes[l], mesh_member(f2) + MESH_M_STATE, ~0u, MESH_MS_ABSENT, 3000));
    CHECK(wait_u32(&fakes[l], MESH_S_UP_MASK, ~0u, two, 3000));
    /* Two of three are a majority: changes still commit. */
    CHECK(change((uint32_t)l, "sched uncordon 2", 15000, NULL));

    /* It comes back and catches up. */
    start(f2);
    CHECK(wait_alive((uint32_t)l, (1u << N) - 1, 5000));
    v = mpr_version(relays[l]);
    CHECK(wait_applied(f2, v, 5000));
    pthread_mutex_lock(&fakes[f2].mu);
    CHECK(fakes[f2].cordon == 0);
    pthread_mutex_unlock(&fakes[f2].mu);

    /* The leader stops: the two left elect one of them and go on committing. */
    stop((uint32_t)l);
    int l2 = wait_leader(10000);
    CHECK(l2 >= 0 && l2 != l);
    CHECK(change(l2 == (int)f1 ? f2 : f1, "sched pause", 15000, NULL));
    v = mpr_version(relays[l2]);
    CHECK(wait_applied(f1, v, 5000) && wait_applied(f2, v, 5000));
    CHECK(wait_u32(&fakes[f1], MESH_S_SCHED_FLAGS, MESH_SF_PAUSED, MESH_SF_PAUSED, 3000));

    /* One of three left: no majority, no leader, a change is refused. */
    uint32_t last = l2 == (int)f1 ? f2 : f1;
    stop((uint32_t)l2);
    end = mono_ms() + 5000;
    uint32_t x;
    while (mpr_leader(relays[last], &x) && mono_ms() < end) sleep_ms(20);
    CHECK(!mpr_leader(relays[last], &x));
    CHECK(!change(last, "sched resume", 15000, &text));
    CHECK(text && strstr(text, "unchanged"));
    free(text);
    CHECK(wait_u32(&fakes[last], MESH_S_FLEET_FLAGS, MESH_FF_QUORUM, 0, 3000));
    CHECK(wait_u32(&fakes[last], MESH_S_LEADER, ~0u, MESH_PEER_NONE, 3000));
    stop(last);

    for (uint32_t k = 0; k < N; k++) {
        CHECK(!fakes[k].failed);
        free(fakes[k].mb), free(fakes[k].fleet), free(fakes[k].groups), free(fakes[k].sched), free(fakes[k].recs);
    }
    free(conf);
    return mesh_check_done();
}
