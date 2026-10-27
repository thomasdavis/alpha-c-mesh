/* A CPU-only fleet: three whole meshds (--backend wasm) in child processes on loopback, members of
 * one cluster config, driven over their control ports like mesh-fleetcheck drives a fleet:
 * - they elect one leader and agree on it, its term and the committed version (`fleet`);
 * - `load` and `start` asked of a follower go through the leader, commit, and every member runs the
 *   program (the reconciler, its image from the local catalog);
 * - `job submit hashjob items N` on a follower runs on the members' wasm lanes to the end, and
 *   `job results` holds every item exactly once, each result and job_out recomputed here;
 * - `jobs` answers on every member; the HTTP relay answers;
 * - a member killed (SIGKILL) is seen down by the others, the two left keep a leader and commit a
 *   change, and they exit 0 on SIGTERM. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "meshd.h"
#include "mesh/abi.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"
#include "mesh/ref/sha256d.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define N 3
#define ITEMS 240

static void sleep_ms(long ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static int free_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (struct sockaddr *)&sa, sizeof sa);
    socklen_t sl = sizeof sa;
    getsockname(fd, (struct sockaddr *)&sa, &sl);
    close(fd);
    return ntohs(sa.sin_port);
}

static int conn(int port) {
    for (int i = 0; i < 500; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET;
        sa.sin_port = htons((uint16_t)port);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) return fd;
        close(fd);
        sleep_ms(20);
    }
    return -1;
}

static int CTL[N], HTTP[N], PEER[N];
static pid_t PID[N];
static bool ALIVE[N];

/* One request on a fresh connection to member k's control port: a line ("ok ..."/"err ..."), or a
 * framed reply (framed). NULL when the member does not answer. */
static char *request(int k, const char *req, bool framed) {
    int fd = conn(CTL[k]);
    if (fd < 0) return NULL;
    struct timeval tv = {120, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (write(fd, req, strlen(req)) < 0 || write(fd, "\n", 1) < 0) {
        close(fd);
        return NULL;
    }
    char *b = NULL;
    if (framed) {
        uint8_t h[4];
        size_t got = 0;
        while (got < 4) {
            ssize_t r = read(fd, h + got, 4 - got);
            if (r <= 0) break;
            got += (size_t)r;
        }
        if (got == 4) {
            size_t n = (size_t)h[0] | (size_t)h[1] << 8 | (size_t)h[2] << 16 | (size_t)h[3] << 24;
            b = malloc(n + 1);
            size_t at = 0;
            while (at < n) {
                ssize_t r = read(fd, b + at, n - at);
                if (r <= 0) break;
                at += (size_t)r;
            }
            b[at] = 0;
        }
    } else {
        b = malloc(65536);
        size_t i = 0;
        while (i < 65535 && read(fd, b + i, 1) == 1)
            if (b[i++] == '\n') break;
        b[i] = 0;
    }
    close(fd);
    return b;
}

/* The value after key in a line of words ("key value"), into out. */
static bool kv(const char *line, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key);
    for (const char *p = line; (p = strstr(p, key)); p += kl) {
        if ((p == line || p[-1] == ' ') && p[kl] == ' ') {
            const char *v = p + kl + 1;
            size_t n = strcspn(v, " \n");
            if (n >= cap) n = cap - 1;
            memcpy(out, v, n);
            out[n] = 0;
            return true;
        }
    }
    return false;
}

typedef struct {
    char leader[8], up[32], role[16];
    unsigned long long term, version, applied;
} view;

static bool fleet_view(int k, view *v) {
    char *t = request(k, "fleet", true);
    if (!t) return false;
    unsigned sys, n, q;
    unsigned long long acc;
    bool ok = sscanf(t, "fleet system %u of %u role %15s leader %7s term %llu version %llu accepted %llu applied %llu quorum %u up %31s ", &sys, &n,
                     v->role, v->leader, &v->term, &v->version, &acc, &v->applied, &q, v->up) == 10;
    free(t);
    return ok;
}

/* The members in ks agree on a leader, its term and the committed version, all applied, every one up
 * (mesh-fleetcheck's `agreed`): the leader, or -1. */
static int agreed(const int *ks, int nk, const char *up) {
    view v0, v;
    if (!fleet_view(ks[0], &v0) || !strcmp(v0.leader, "-")) return -1;
    for (int i = 0; i < nk; i++) {
        if (!fleet_view(ks[i], &v)) return -1;
        if (strcmp(v.leader, v0.leader) || v.term != v0.term || v.version != v0.version || v.applied != v.version || strcmp(v.up, up)) return -1;
    }
    return atoi(v0.leader);
}

static int wait_agreed(const int *ks, int nk, const char *up, long ms) {
    long end = now_ms() + ms;
    int l;
    while ((l = agreed(ks, nk, up)) < 0 && now_ms() < end) sleep_ms(100);
    return l;
}

/* A change through member k: "ok ..." (retried while the fleet is busy or the leader does not yet know
 * the program runs: mesh-fleetcheck's submit). */
static char *change(int k, const char *req) {
    long end = now_ms() + 30000;
    for (;;) {
        char *r = request(k, req, false);
        if (!r || strncmp(r, "ok", 2) == 0 || now_ms() > end) return r;
        if (!strstr(r, "no member runs") && !strstr(r, "merging") && !strstr(r, "busy") && !strstr(r, "pending") && !strstr(r, "no leader")) return r;
        free(r);
        sleep_ms(300);
    }
}

/* hashjob's item i: the fold over K = 1024 of sha256d(LE8(i * K + j))'s first LE u32, and the first 16
 * bytes of sha256d(LE8(i)) (its job_out). */
static uint32_t hashjob(uint64_t i, uint8_t out[16]) {
    uint32_t fold = 0;
    uint8_t h[32], m[8];
    for (uint64_t j = 0; j < 1024; j++) {
        uint64_t x = i * 1024 + j;
        for (int b = 0; b < 8; b++) m[b] = (uint8_t)(x >> (8 * b));
        mesh_sha256d(m, 8, h);
        fold ^= (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
    }
    if (fold == MESH_R_JOB_FAIL) fold--;
    for (int b = 0; b < 8; b++) m[b] = (uint8_t)(i >> (8 * b));
    mesh_sha256d(m, 8, h);
    memcpy(out, h, 16);
    return fold;
}

static void copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb"), *out = fopen(to, "wb");
    char buf[65536];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, k, out);
    fclose(in);
    fclose(out);
}

static pid_t spawn_member(int k, const char *root, const char *cat, const char *conf) {
    char sys[8], ctl[32], http[32], fleet[256], logs[256], logf[256];
    snprintf(sys, sizeof sys, "%d", k);
    snprintf(ctl, sizeof ctl, "127.0.0.1:%d", CTL[k]);
    snprintf(http, sizeof http, "127.0.0.1:%d", HTTP[k]);
    snprintf(fleet, sizeof fleet, "%s/m%d.fleet", root, k);
    snprintf(logs, sizeof logs, "%s/logs%d", root, k);
    snprintf(logf, sizeof logf, "%s/meshd%d.log", root, k);
    char *argv[] = {"meshd", "--backend", "wasm", "--programs", (char *)cat, "--cluster", (char *)conf, "--system", sys, "--fleet-state", fleet,
                    "--control", ctl, "--http", http, "--logs", logs, NULL};
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open(logf, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        dup2(fd, 2);
        dup2(fd, 1);
        meshd_main((int)(sizeof argv / sizeof *argv) - 1, argv);
    }
    return pid;
}

static int wait_exit(pid_t pid) {
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

static void stop_all(void) {
    for (int k = 0; k < N; k++)
        if (ALIVE[k]) kill(PID[k], SIGKILL), waitpid(PID[k], NULL, 0);
}

/* The job's line in `jobs` on member k: its state word, or "" */
static bool job_state(int k, const char *id, char *state, size_t cap, unsigned long long *done) {
    char *t = request(k, "jobs", true);
    if (!t) return false;
    bool found = false;
    for (char *l = t; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
        char w[64];
        if (strncmp(l, "job ", 4) || sscanf(l + 4, "%63s", w) != 1 || strcmp(w, id)) continue;
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        found = kv(l, "state", state, cap);
        char d[32] = "0";
        const char *items = strstr(l, " items ");
        if (items) kv(items, "done", d, sizeof d);
        *done = strtoull(d, NULL, 10);
        break;
    }
    free(t);
    return found;
}

int main(void) {
    char *wasm = mesh_kit_artifact("ajax-mesh-prog-hashjob-wasm");
    if (!wasm) {
        fprintf(stderr, "meshd cluster test: programs/hashjob-wasm is not built (run `sexc test` for the workspace); skipped\n");
        return 0;
    }
    char root[128], cat[192], path[320], conf[192];
    snprintf(root, sizeof root, "/tmp/meshd-fleet-%d", (int)getpid());
    snprintf(cat, sizeof cat, "%s/programs", root);
    snprintf(conf, sizeof conf, "%s/cluster.conf", root);
    mkdir(root, 0700);
    mkdir(cat, 0700);
    snprintf(path, sizeof path, "%s/hashjob", cat);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/hashjob/program.toml", cat);
    FILE *f = fopen(path, "w");
    fputs("name       = \"hashjob\"\nabi        = 6\nbackends   = [\"wasm\"]\nview_bytes = 0x1000\nnodes      = 4\npolicy     = \"job\"\nfanout     = 4\n"
          "param      = 1024\njob_base   = 0x08\njob_out    = \"0x100:16\"\ncounters   = [\"items\", \"khashes\", \"failed\", \"\"]\n"
          "fields     = [\"items:u64:0x20\", \"hashes:u64:0x28\", \"last:u64:0x30\", \"fail_item:u32:0x10\", \"trap_item:u32:0x18\"]\n"
          "task_ms    = 2000\nautostart  = false\n",
          f);
    fclose(f);
    snprintf(path, sizeof path, "%s/hashjob/hashjob.wasm", cat);
    copy_file(wasm, path);
    free(wasm);

    for (int k = 0; k < N; k++) CTL[k] = free_port(), HTTP[k] = free_port(), PEER[k] = free_port();
    f = fopen(conf, "w");
    fprintf(f, "# a CPU-only fleet on loopback\nsystems = %d\nping_ms = 100\ndead_ms = 1500\n", N);
    for (int k = 0; k < N; k++) fprintf(f, "%d.peer = 127.0.0.1:%d\n", k, PEER[k]);
    fclose(f);
    for (int k = 0; k < N; k++) {
        PID[k] = spawn_member(k, root, cat, conf);
        ALIVE[k] = true;
    }
    atexit(stop_all);

    /* One leader, agreed by all three, every member up. */
    int all[N] = {0, 1, 2};
    int leader = wait_agreed(all, N, "0,1,2", 60000);
    CHECK(leader >= 0);
    if (leader < 0) return 1;
    int follower = (leader + 1) % N;
    view v;
    CHECK(fleet_view(follower, &v) && !strcmp(v.role, "follower"));
    CHECK(fleet_view(leader, &v) && !strcmp(v.role, "leader"));
    fprintf(stderr, "cluster: leader %d, term %llu\n", leader, v.term);

    /* The program on the fleet, asked of a follower: committed, reconciled and running everywhere. */
    char *r = change(follower, "load hashjob");
    CHECK(r && strncmp(r, "ok", 2) == 0);
    fprintf(stderr, "load hashjob: %s", r ? r : "(none)\n");
    free(r);
    r = change(follower, "start hashjob");
    CHECK(r && strncmp(r, "ok", 2) == 0);
    fprintf(stderr, "start hashjob: %s", r ? r : "(none)\n");
    free(r);
    CHECK(wait_agreed(all, N, "0,1,2", 30000) == leader);
    for (int k = 0; k < N; k++) {
        char *p = request(k, "programs", true);
        CHECK(p && strstr(p, " hashjob running "));
        free(p);
    }

    /* A job array through a follower: to the end, every item exactly once and right. */
    char req[64];
    snprintf(req, sizeof req, "job submit hashjob items %d", ITEMS);
    r = change(follower, req);
    CHECK(r && strncmp(r, "ok job ", 7) == 0);
    fprintf(stderr, "%s: %s", req, r ? r : "(none)\n");
    char id[32] = "";
    if (r) sscanf(r, "ok job %31s", id);
    free(r);
    char state[32] = "";
    unsigned long long done = 0;
    long end = now_ms() + 300000;
    while (now_ms() < end) {
        if (job_state(leader, id, state, sizeof state, &done) && !strncmp(state, "done", 4)) break;
        sleep_ms(250);
    }
    CHECK_S(state, "done");
    CHECK_U(done, ITEMS);
    for (int k = 0; k < N; k++) {
        char s2[32] = "";
        unsigned long long d2 = 0;
        long e2 = now_ms() + 10000;
        while (now_ms() < e2 && !(job_state(k, id, s2, sizeof s2, &d2) && !strncmp(s2, "done", 4))) sleep_ms(100);
        CHECK_S(s2, "done");
    }
    snprintf(req, sizeof req, "job results %s 0 100000", id);
    char *res = request(follower, req, true);
    CHECK(res && strncmp(res, "results ", 8) == 0);
    static uint8_t seen[ITEMS];
    unsigned per[N] = {0};
    int lines = 0, bad = 0, dup = 0;
    for (char *l = res ? strchr(res, '\n') : NULL; l && l[1]; l = strchr(l + 1, '\n')) {
        unsigned long long item;
        unsigned result;
        if (sscanf(l + 1, "%llu %u", &item, &result) != 2) continue;
        char *e = strchr(l + 1, '\n');
        if (e) *e = 0;
        char out[64] = "", mem[16] = "";
        kv(l + 1, "out", out, sizeof out);
        kv(l + 1, "member", mem, sizeof mem);
        if (e) *e = '\n';
        lines++;
        if (item >= ITEMS) {
            bad++;
            continue;
        }
        if (seen[item]++) dup++;
        uint8_t want[16];
        char hex[33];
        uint32_t fold = hashjob(item, want);
        mesh_sha_hex(want, 16, hex);
        if (fold != result || strcmp(out, hex)) bad++;
        unsigned m = (unsigned)atoi(mem);
        if (m < N) per[m]++;
    }
    free(res);
    int missing = 0;
    for (int i = 0; i < ITEMS; i++) missing += !seen[i];
    CHECK_I(lines, ITEMS);
    CHECK_I(dup, 0);
    CHECK_I(missing, 0);
    CHECK_I(bad, 0);
    fprintf(stderr, "job %s: %d items exactly once, every result and job_out recomputed: equal; per member %u / %u / %u\n", id, lines, per[0], per[1],
            per[2]);

    /* `jobs` on every member; the HTTP relay answers. */
    for (int k = 0; k < N; k++) {
        char *j = request(k, "jobs", true);
        CHECK(j && strncmp(j, "jobs system ", 12) == 0);
        free(j);
    }
    int hfd = conn(HTTP[follower]);
    CHECK(hfd >= 0);
    const char *get = "GET /status HTTP/1.0\r\nHost: x\r\n\r\n";
    CHECK(write(hfd, get, strlen(get)) == (ssize_t)strlen(get));
    char hb[64] = "";
    struct timeval tv = {30, 0};
    setsockopt(hfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ssize_t hn = read(hfd, hb, sizeof hb - 1);
    hb[hn > 0 ? hn : 0] = 0;
    CHECK(strncmp(hb, "HTTP/1.", 7) == 0);
    close(hfd);

    /* A member killed: the others see it down, keep their leader (or elect one) and commit. */
    int victim = leader == 2 ? 1 : 2;
    kill(PID[victim], SIGKILL);
    waitpid(PID[victim], NULL, 0);
    ALIVE[victim] = false;
    int rest[2], nr = 0;
    for (int k = 0; k < N; k++)
        if (k != victim) rest[nr++] = k;
    char up2[16];
    snprintf(up2, sizeof up2, "%d,%d", rest[0], rest[1]);
    int l2 = wait_agreed(rest, 2, up2, 30000);
    CHECK(l2 >= 0 && l2 != victim);
    for (int i = 0; i < 2; i++) {
        char *t = request(rest[i], "fleet", true);
        char want[32];
        snprintf(want, sizeof want, "\nmember %d down ", victim);
        CHECK(t && strstr(t, want));
        free(t);
    }
    r = change(rest[0], "stop hashjob");
    CHECK(r && strncmp(r, "ok", 2) == 0);
    fprintf(stderr, "member %d killed: seen down; leader %d; stop hashjob: %s", victim, l2, r ? r : "(none)\n");
    free(r);
    CHECK(wait_agreed(rest, 2, up2, 30000) == l2);

    for (int i = 0; i < 2; i++) kill(PID[rest[i]], SIGTERM);
    for (int i = 0; i < 2; i++) {
        CHECK_I(wait_exit(PID[rest[i]]), 0);
        ALIVE[rest[i]] = false;
    }
    char cmd[256];
    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    if (mesh_check_failures == 0 && system(cmd) != 0) fprintf(stderr, "could not remove %s\n", root);
    else if (mesh_check_failures) fprintf(stderr, "logs kept in %s\n", root);
    return mesh_check_done();
}
