/* mesh-worker tests/lifecycle.rs: the native executor against the fixture
 * worker (apps/mesh-test-worker) in a real shared mailbox: spawn and `ready`,
 * launch, tasks, a halt that keeps the process (relaunch, same pid), a stop
 * that ends it (reaped, no crash), a crash (FAILED reason: status, output
 * tail), deaths mid-task, refusals (no binary, not executable, no
 * source.hash, a script, a peer's hash, abi), identities, and on Linux the
 * memory cap (OOM kill) and the CPU cap where systemd's user scopes work. */
#include "mesh/check.h"
#include "mesh/mailbox.h"
#include "mesh/manifest_encode.h"
#include "mesh/native.h"
#include "mesh/proc_usage.h"
#include "mesh/text.h"

#include "mesh/abi/control_block.h"
#include "mesh/abi/ext.h"
#include "mesh/abi/header.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/ring.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char *fixture;

typedef struct {
    char root[PATH_MAX];
    mesh_shm shm;
    mesh_mailbox mb;
    mesh_backend *b;
    mesh_native_host *host;
    const mesh_compiler *comp;
} env;

static void run(char *const argv[]) {
    pid_t p = fork();
    if (p == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    waitpid(p, &st, 0);
}

static void env_new(env *e, const char *tag) {
    snprintf(e->root, sizeof e->root, "/tmp/mesh-native-%s-%d", tag, (int)getpid());
    run((char *[]){"rm", "-rf", e->root, NULL});
    mkdir(e->root, 0755);
    char *err = NULL;
    if (!mesh_shm_new(&e->shm, &err)) {
        fprintf(stderr, "%s\n", err);
        exit(1);
    }
    e->mb = mesh_mailbox_new(e->shm.base, 0);
    mesh_mailbox_set(e->mb, MESH_H_MAGIC, MESH_MAGIC);
    mesh_mailbox_set(e->mb, MESH_H_VERSION, MESH_VERSION);
    char *logs = mesh_fmt("%s/logs", e->root);
    mesh_worker_cfg cfg = {0};
    e->b = mesh_native_backend_new(&e->shm, logs, &cfg);
    free(logs);
    e->host = mesh_native_backend_host(e->b);
    e->comp = e->b->ops->compiler(e->b);
}

static void env_end(env *e) { run((char *[]){"rm", "-rf", e->root, NULL}); }

static void write_file(const char *path, const char *s) {
    FILE *f = fopen(path, "w");
    fputs(s, f);
    fclose(f);
}

static mesh_payload catalog(env *e, const char *dir, const char *manifest) {
    char *d = mesh_fmt("%s/%s", e->root, dir), *t = mesh_fmt("%s/%s", d, mesh_native_target());
    mkdir(d, 0755);
    mkdir(t, 0755);
    char *p = mesh_fmt("%s/program.toml", d), *h = mesh_fmt("%s/source.hash", d), *bin = mesh_native_binary(d, "nt");
    write_file(p, manifest);
    write_file(h, "0123456789abcdef\n");
    run((char *[]){"cp", fixture, bin, NULL});
    free(d);
    free(t);
    free(p);
    free(h);
    free(bin);
    return (mesh_payload){mesh_strdup(dir), mesh_strdup(manifest), NULL, 0};
}

static char *manifest(const char *extra) {
    return mesh_fmt("name = \"nt\"\nabi = 6\nbackends = [\"native\"]\nview_bytes = 0x1000\n%s%s", strstr(extra, "nodes") ? "" : "nodes = 2\n", extra);
}

/* Prepared and installed in slot (the table entry meshd writes): the worker kernel. */
static mesh_kernel *prepare(env *e, mesh_payload p, uint32_t slot, char **err) {
    mesh_prepared pr;
    if (!e->comp->ops->prepare(e->comp, &p, NULL, e->root, &pr, err)) return NULL;
    mesh_image im = {0x100000, 0, pr.hash, 0, 0, 0, 1};
    uint8_t entry[MESH_PT_BYTES], ext[MESH_PX_BYTES];
    mesh_manifest_encode_entry(&pr.manifest, &im, entry);
    mesh_manifest_encode_ext(&pr.manifest, ext);
    for (size_t i = 2; i < MESH_PT_BYTES / 4; i++) {
        uint32_t w;
        memcpy(&w, entry + 4 * i, 4);
        mesh_mailbox_set(e->mb, mesh_prog_entry(slot) + 4 * i, w);
    }
    for (size_t i = 0; i < MESH_PX_BYTES / 4; i++) {
        uint32_t w;
        memcpy(&w, ext + 4 * i, 4);
        mesh_mailbox_set(e->mb, mesh_prog_ext(slot) + 4 * i, w);
    }
    mesh_mailbox_set(e->mb, mesh_prog_entry(slot) + MESH_PT_STATE, MESH_ST_LOADED);
    mesh_kernel *k = pr.kernel;
    pr.kernel = NULL;
    mesh_prepared_free(&pr);
    return k;
}

static mesh_native_worker *W(mesh_kernel *k) { return mesh_kernel_native(k); }

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static void launch(env *e, mesh_kernel *k, uint32_t slot, uint32_t tail) {
    size_t c = mesh_prog_ctl(slot);
    mesh_mailbox_set(e->mb, c + MESH_P_RING_HEAD, tail);
    mesh_mailbox_set(e->mb, c + MESH_P_ONLINE, 0);
    mesh_mailbox_set(e->mb, c + MESH_P_HALTED, 0);
    mesh_mailbox_publish(e->mb, c + MESH_P_SHUTDOWN, 0);
    char *err = NULL;
    CHECK(mesh_native_worker_launch(W(k), slot, &err));
    if (err) fprintf(stderr, "launch: %s\n", err);
    free(err);
}

#define WAIT(what, ms, cond)                                                     \
    do {                                                                         \
        uint64_t t0_ = now_ms();                                                 \
        while (!(cond)) {                                                        \
            if (now_ms() - t0_ > (ms)) {                                         \
                CHECK(!what);                                                    \
                break;                                                           \
            }                                                                    \
            usleep(200);                                                         \
        }                                                                        \
    } while (0)

/* Publishes (op, arg) as ticket t and waits for its result; false with *why on a crash. */
static bool task(env *e, mesh_kernel *k, uint32_t slot, uint32_t t, uint32_t op, uint32_t arg, uint32_t *res, char **why) {
    size_t c = mesh_prog_ctl(slot), en = c + MESH_P_RING + MESH_ENTRY_BYTES * (t % MESH_PROG_RING), kk = t % MESH_PROG_RESULTS;
    mesh_mailbox_set(e->mb, en + MESH_E_OP, op);
    mesh_mailbox_set(e->mb, en + MESH_E_ARG, arg);
    mesh_mailbox_set(e->mb, en + MESH_E_SLOT, t % MESH_PROG_RESULTS);
    mesh_mailbox_publish(e->mb, en + MESH_E_SEQ, t + 1);
    uint64_t t0 = now_ms();
    for (;;) {
        if (mesh_mailbox_get(e->mb, c + MESH_P_MARKS + 4 * kk) == t + 1) {
            *res = mesh_mailbox_get(e->mb, c + MESH_P_RESULTS + 4 * kk);
            return true;
        }
        char *cr = mesh_native_worker_crashed(W(k));
        if (cr) {
            *why = cr;
            return false;
        }
        if (now_ms() - t0 > 20000) {
            *why = mesh_fmt("ticket %u: no completion", (unsigned)t);
            return false;
        }
        usleep(100);
    }
}

static uint32_t ok_task(env *e, mesh_kernel *k, uint32_t slot, uint32_t t, uint32_t op, uint32_t arg) {
    uint32_t r = 0;
    char *why = NULL;
    if (!CHECK(task(e, k, slot, t, op, arg, &r, &why))) {
        fprintf(stderr, "task %u: %s\n", (unsigned)t, why);
        free(why);
    }
    return r;
}

static char *err_task(env *e, mesh_kernel *k, uint32_t slot, uint32_t t, uint32_t op, uint32_t arg) {
    uint32_t r;
    char *why = NULL;
    CHECK(!task(e, k, slot, t, op, arg, &r, &why));
    return why ? why : mesh_strdup("");
}

static bool alive(pid_t pid) {
    uint64_t c, r;
    return mesh_proc_usage(pid, &c, &r);
}

static void spawn_tasks_halt_relaunch_stop(void) {
    env e;
    env_new(&e, "life");
    char *m = manifest("param = 2"), *err = NULL;
    mesh_kernel *k = prepare(&e, catalog(&e, "nt", m), 5, &err);
    free(m);
    if (!CHECK(k)) {
        fprintf(stderr, "%s\n", err);
        return;
    }
    /* A start after load with no process: spawned off the calling thread. */
    uint64_t t0 = now_ms();
    launch(&e, k, 5, 0);
    CHECK(now_ms() - t0 < 50);
    WAIT("online", 20000, mesh_mailbox_get(e.mb, mesh_prog_ctl(5) + MESH_P_ONLINE) == 2);
    CHECK_U(ok_task(&e, k, 5, 0, MESH_OP_INIT, 0), MESH_R_NONE);
    CHECK_U(mesh_mailbox_get(e.mb, 0x100000), 0x57544e4e);
    for (uint32_t t = 1; t < 50; t++) CHECK_U(ok_task(&e, k, 5, t, MESH_OP_TASK, t), t * 3 + 7 + 2000);
    pid_t pid = (pid_t)ok_task(&e, k, 5, 50, 5, 0);
    CHECK(!mesh_native_worker_query(W(k)));
    /* A halt (a blink): the nodes stop, the process stays for the next run. */
    mesh_mailbox_publish(e.mb, mesh_prog_ctl(5) + MESH_P_SHUTDOWN, MESH_SHUTDOWN_HALT);
    WAIT("halted", 5000, mesh_native_worker_query(W(k)));
    CHECK_U(mesh_mailbox_get(e.mb, mesh_prog_ctl(5) + MESH_P_HALTED), 2);
    CHECK(alive(pid));
    launch(&e, k, 5, 51);
    WAIT("online again", 5000, mesh_mailbox_get(e.mb, mesh_prog_ctl(5) + MESH_P_ONLINE) == 2);
    CHECK_I(ok_task(&e, k, 5, 51, 5, 0), pid);
    mesh_native_stats st = mesh_native_worker_stats(W(k));
    CHECK(st.pid == pid && st.spawns == 1 && st.crashes == 0);
    /* A stop: halt and exit; reaped, no crash. */
    mesh_mailbox_publish(e.mb, mesh_prog_ctl(5) + MESH_P_SHUTDOWN, MESH_SHUTDOWN_EXIT);
    WAIT("stopped", 5000, mesh_native_worker_query(W(k)));
    char *cr = mesh_native_worker_crashed(W(k));
    CHECK(cr == NULL);
    if (cr) fprintf(stderr, "crashed: %s\n", cr);
    free(cr);
    WAIT("reaped", 2000, !alive(pid));
    /* Its output went to the log file. */
    char *lp = mesh_fmt("%s/logs/nt.log", e.root), *want = mesh_fmt("test worker %d set up", (int)pid);
    FILE *f = fopen(lp, "r");
    char log[8192] = {0};
    if (f) {
        size_t n = fread(log, 1, sizeof log - 1, f);
        log[n] = 0;
        fclose(f);
    }
    CHECK(strstr(log, want) != NULL);
    free(lp);
    free(want);
    /* The next start spawns a new process. */
    launch(&e, k, 5, 52);
    WAIT("online after a stop", 20000, mesh_mailbox_get(e.mb, mesh_prog_ctl(5) + MESH_P_ONLINE) == 2);
    pid_t pid2 = (pid_t)ok_task(&e, k, 5, 52, 5, 0);
    CHECK(pid2 != pid);
    CHECK_U(mesh_native_worker_stats(W(k)).spawns, 2);
    mesh_native_worker_release(W(k));
    WAIT("released", 5000, !alive(pid2));
    mesh_kernel_drop(k);
    env_end(&e);
}

static void warm_then_launch_is_immediate(void) {
    env e;
    env_new(&e, "warm");
    char *m = manifest(""), *err = NULL;
    mesh_kernel *k = prepare(&e, catalog(&e, "nt", m), 1, &err);
    free(m);
    CHECK(mesh_native_worker_warm(W(k), &err));
    pid_t pid = mesh_native_worker_stats(W(k)).pid;
    CHECK(pid != 0);
    uint64_t t0 = now_ms();
    launch(&e, k, 1, 0);
    WAIT("online", 5000, mesh_mailbox_get(e.mb, mesh_prog_ctl(1) + MESH_P_ONLINE) == 2);
    uint64_t up = now_ms() - t0;
    CHECK_I(ok_task(&e, k, 1, 0, 5, 0), pid);
    CHECK(up < 100);
    mesh_kernel_drop(k);
    env_end(&e);
}

static void crashes_fail_with_status_and_output(void) {
    env e;
    env_new(&e, "crash");
    char *m = manifest(""), *err = NULL;
    mesh_kernel *k = prepare(&e, catalog(&e, "nt", m), 2, &err);
    free(m);
    launch(&e, k, 2, 0);
    WAIT("online", 20000, mesh_mailbox_get(e.mb, mesh_prog_ctl(2) + MESH_P_ONLINE) == 2);
    char *r = err_task(&e, k, 2, 0, 9, 42);
    CHECK(strstr(r, "exit 101") && strstr(r, "deliberate crash (op 9, arg 42)") && strstr(r, "while running"));
    fprintf(stderr, "crash report: %s\n", r);
    free(r);
    CHECK(mesh_native_worker_query(W(k)));
    mesh_native_stats st = mesh_native_worker_stats(W(k));
    CHECK(st.crashes == 1 && st.pid == 0);
    char *again = mesh_native_worker_crashed(W(k));
    CHECK(again == NULL);
    free(again);
    /* exit(3) mid-task is a crash too, and so is a signal; the next launch is a new process. */
    launch(&e, k, 2, 1);
    WAIT("online", 20000, mesh_mailbox_get(e.mb, mesh_prog_ctl(2) + MESH_P_ONLINE) == 2);
    r = err_task(&e, k, 2, 1, 13, 3);
    CHECK(strstr(r, "exit 3") != NULL);
    free(r);
    launch(&e, k, 2, 2);
    WAIT("online", 20000, mesh_mailbox_get(e.mb, mesh_prog_ctl(2) + MESH_P_ONLINE) == 2);
    r = err_task(&e, k, 2, 2, 13, 0);
    CHECK(strstr(r, "signal 15") != NULL);
    free(r);
    CHECK_U(mesh_native_worker_stats(W(k)).crashes, 3);
    mesh_kernel_drop(k);
    env_end(&e);
}

static char *prepare_err(env *e, mesh_payload p, const uint64_t *expect) {
    mesh_prepared pr;
    char *err = NULL;
    if (e->comp->ops->prepare(e->comp, &p, expect, e->root, &pr, &err)) {
        mesh_prepared_free(&pr);
        return mesh_strdup("(prepared)");
    }
    return err;
}

static void refusals(void) {
    env e;
    env_new(&e, "refuse");
    char *m = manifest(""), *r, *d, *b;
    /* No binary for this target: `unsupported`. */
    mesh_payload p = catalog(&e, "nobin", m);
    d = mesh_fmt("%s/nobin", e.root);
    b = mesh_native_binary(d, "nt");
    unlink(b);
    r = prepare_err(&e, p, NULL);
    CHECK(strncmp(r, "unsupported: nt has no binary for", 33) == 0);
    free(r), free(d), free(b);
    /* Not executable. */
    p = catalog(&e, "noexec", m);
    d = mesh_fmt("%s/noexec", e.root);
    b = mesh_native_binary(d, "nt");
    chmod(b, 0644);
    r = prepare_err(&e, p, NULL);
    CHECK(strstr(r, "not executable") != NULL);
    free(r), free(d), free(b);
    /* A script is not an executable of this target. */
    p = catalog(&e, "script", m);
    d = mesh_fmt("%s/script", e.root);
    b = mesh_native_binary(d, "nt");
    write_file(b, "#!/bin/sh\necho hi\n");
    r = prepare_err(&e, p, NULL);
    CHECK(strstr(r, "not an executable for") != NULL);
    free(r), free(d), free(b);
    /* No source.hash: no identity. */
    p = catalog(&e, "nohash", m);
    d = mesh_fmt("%s/nohash/source.hash", e.root);
    unlink(d);
    r = prepare_err(&e, p, NULL);
    CHECK(strncmp(r, "manifest: nt:", 13) == 0);
    free(r), free(d);
    /* A peer's hash must match. */
    uint64_t one = 1;
    r = prepare_err(&e, catalog(&e, "hash", m), &one);
    CHECK(strncmp(r, "peer:", 5) == 0);
    free(r);
    /* abi. */
    char *m5 = mesh_fmt("name = \"nt\"\nabi = 5\nbackends = [\"native\"]\nview_bytes = 0x1000\nnodes = 2\n");
    r = prepare_err(&e, catalog(&e, "abi", m5), NULL);
    CHECK(strncmp(r, "abi:", 4) == 0);
    free(r), free(m5), free(m);
    env_end(&e);
}

static uint64_t prepared_hash(env *e, mesh_payload p, const uint64_t *expect, bool *ok) {
    mesh_prepared pr;
    char *err = NULL;
    *ok = e->comp->ops->prepare(e->comp, &p, expect, e->root, &pr, &err);
    free(err);
    if (!*ok) return 0;
    uint64_t h = pr.hash;
    mesh_prepared_free(&pr);
    return h;
}

static void identity_is_the_manifest_and_source(void) {
    env e;
    env_new(&e, "ident");
    char *m = manifest(""), *mp = manifest("param = 1");
    bool ok;
    uint64_t a = prepared_hash(&e, catalog(&e, "a", m), NULL, &ok);
    uint64_t b = prepared_hash(&e, catalog(&e, "b", m), NULL, &ok);
    uint64_t c = prepared_hash(&e, catalog(&e, "c", mp), NULL, &ok);
    CHECK(a == b);
    CHECK(a != c);
    prepared_hash(&e, catalog(&e, "d", m), &a, &ok);
    CHECK(ok);
    free(m);
    free(mp);
    env_end(&e);
}

static void memory_cap_kills(void) {
    mesh_native_limits l = {0, 64u << 20, 0, false};
    char *err = NULL;
    if (!mesh_native_limits_available(&l, &err)) {
        printf("memory_cap_kills: skipped (%s)\n", err);
        free(err);
        return;
    }
    env e;
    env_new(&e, "mem");
    char *m = manifest("mem_mb = 64");
    mesh_kernel *k = prepare(&e, catalog(&e, "nt", m), 3, &err);
    free(m);
    if (!CHECK(k)) {
        fprintf(stderr, "%s\n", err);
        return;
    }
    launch(&e, k, 3, 0);
    WAIT("online", 20000, mesh_mailbox_get(e.mb, mesh_prog_ctl(3) + MESH_P_ONLINE) == 2);
    CHECK_U(mesh_native_worker_stats(W(k)).limits, MESH_LIMIT_MEM);
    CHECK_U(ok_task(&e, k, 3, 0, 10, 16), 16);
    char *r = err_task(&e, k, 3, 1, 10, 256);
    CHECK(strstr(r, "signal 9") != NULL);
    free(r);
    mesh_kernel_drop(k);
    env_end(&e);
}

static void cpu_cap_holds(void) {
    mesh_native_limits l = {50, 0, 0, false};
    char *err = NULL;
    if (!mesh_native_limits_available(&l, &err)) {
        printf("cpu_cap_holds: skipped (%s)\n", err);
        free(err);
        return;
    }
    env e;
    env_new(&e, "cpu");
    char *m = manifest("cpu = 0.5\nnodes = 1");
    mesh_kernel *k = prepare(&e, catalog(&e, "nt", m), 4, &err);
    free(m);
    launch(&e, k, 4, 0);
    WAIT("online", 20000, mesh_mailbox_get(e.mb, mesh_prog_ctl(4) + MESH_P_ONLINE) == 1);
    pid_t pid = (pid_t)ok_task(&e, k, 4, 0, 5, 0);
    uint64_t c0, c1, r;
    mesh_proc_usage(pid, &c0, &r);
    uint64_t t0 = now_ms();
    ok_task(&e, k, 4, 1, 12, 1000);
    mesh_proc_usage(pid, &c1, &r);
    double share = (double)(c1 - c0) / ((double)(now_ms() - t0) * 1e6);
    CHECK(share < 0.6);
    CHECK_U(mesh_native_worker_stats(W(k)).limits, MESH_LIMIT_CPU);
    mesh_kernel_drop(k);
    env_end(&e);
}

int main(void) {
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) return 1;
    self[n] = 0;
    /* .sexc/out/<profile>/test/<pkg>/lifecycle -> .sexc/out/<profile>/bin/mesh-test-worker */
    char *s = strstr(self, "/test/");
    if (s) *s = 0;
    fixture = mesh_fmt("%s/bin/mesh-test-worker", self);
    if (access(fixture, X_OK) != 0) {
        printf("skipped: %s is not built (run `sexc build @ajax/mesh-test-worker` first)\n", fixture);
        return 0;
    }
    spawn_tasks_halt_relaunch_stop();
    warm_then_launch_is_immediate();
    crashes_fail_with_status_and_output();
    refusals();
    identity_is_the_manifest_and_source();
    memory_cap_kills();
    cpu_cap_holds();
    mesh_backend_cleanup();
    return mesh_check_done();
}
