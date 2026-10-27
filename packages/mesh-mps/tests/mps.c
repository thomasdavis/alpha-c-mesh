/* The CUDA backend without a GPU, in both isolation modes, over
 * @ajax/cuda-driver-fake (whose kernels play the program's node loop on the
 * real mailbox) and a fake nvJitLink.
 *
 * This one binary is three programs: the test; `nvidia-cuda-mps-control`
 * (argv[0], via a symlink on PATH: a fake control daemon that knows its
 * clients from a file); and `meshd worker` (argv[1]: an MPS worker on the
 * fake device, which registers itself as a client).  So --isolation mps runs
 * for real: a private daemon, a pool of worker processes, preloads, launches
 * through the wire format, halts, the pool's reuse, and the exit cleanup. */
#include "mesh/check.h"
#include "mesh/cuda_fake.h"
#include "mesh/manifest_encode.h"
#include "mesh/mps.h"
#include "mesh/mps_worker.h"
#include "mesh/worker_node.h"

#include "mesh/abi/control_block.h"
#include "mesh/abi/ext.h"
#include "mesh/abi/header.h"
#include "mesh/abi/host_status.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/ring.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ---- the program the fake GPU runs: hello (arg * 3 + 7), with a view magic ---------------- */

static void hello_init(const mesh_worker_ctx *c, void *u) {
    (void)u;
    mesh_ctx_set_u32(c, 0, 0x4f4c4548);
}
static uint32_t hello_task(const mesh_worker_ctx *c, uint32_t op, uint32_t arg, void *u) {
    (void)u;
    if (op != MESH_OP_TASK) return MESH_R_BAD_OP;
    mesh_ctx_count(c, 0, 1);
    return arg * 3 + 7;
}
static const mesh_worker_program hello = {hello_init, hello_task, NULL};

static void kernel(const char *entry, uint64_t mailbox, uint32_t slot, uint32_t nodes, uint32_t threads, void *user) {
    (void)entry, (void)nodes, (void)threads, (void)user;
    mesh_worker_run_nodes(mesh_mailbox_new((uint8_t *)(uintptr_t)mailbox, 0), slot, &hello, NULL);
}

/* ---- a fake nvJitLink: any PTX becomes a fake cubin ------------------------------------------ */

static int j_create(void **h, uint32_t n, const char *const *o) {
    (void)n, (void)o;
    *h = (void *)1;
    return 0;
}
static int j_destroy(void **h) {
    *h = NULL;
    return 0;
}
static int j_add(void *h, int k, const void *d, size_t n, const char *name) {
    (void)h, (void)k, (void)name;
    return n > 0 && ((const char *)d)[n - 1] == 0 && strstr(d, ".entry mesh_main") ? 0 : 5;
}
static int j_complete(void *h) {
    (void)h;
    return 0;
}
static const char cubin[] = "FAKECUBIN entry=mesh_main regs=40 local=64 rt_abi=6";
static int j_size(void *h, size_t *n) {
    (void)h;
    *n = sizeof cubin;
    return 0;
}
static int j_cubin(void *h, void *out) {
    (void)h;
    memcpy(out, cubin, sizeof cubin);
    return 0;
}
static int j_err_size(void *h, size_t *n) {
    (void)h;
    *n = 0;
    return 0;
}
static int j_err_log(void *h, void *o) {
    (void)h, (void)o;
    return 0;
}
static const mesh_nvjitlink_fns jit_fns = {j_create, j_destroy, j_add, j_complete, j_size, j_cubin, j_err_size, j_err_log};

/* ---- the fake control daemon -------------------------------------------------------------- */

static char *pipe_file(const char *name) {
    const char *d = getenv("CUDA_MPS_PIPE_DIRECTORY");
    return mesh_fmt("%s/%s", d ? d : ".", name);
}

static int fake_control(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        pid_t p = fork();
        if (p != 0) return p > 0 ? 0 : 1;
        setsid();
        char *pidf = pipe_file("nvidia-cuda-mps-control.pid"), *ctl = pipe_file("control"), *quit = pipe_file("quit");
        FILE *f = fopen(pidf, "w");
        fprintf(f, "%d\n", (int)getpid());
        fclose(f);
        close(open(ctl, O_CREAT | O_WRONLY, 0600));
        for (int i = 0; i < 600000 && access(quit, F_OK) != 0; i++) usleep(1000);
        unlink(ctl);
        _exit(0);
    }
    char line[128] = {0};
    if (!fgets(line, sizeof line, stdin)) return 1;
    line[strcspn(line, "\n")] = 0;
    char *clients = pipe_file("clients"), *quit = pipe_file("quit");
    int rc = 0;
    if (strcmp(line, "get_server_list") == 0) {
        if (access(clients, F_OK) == 0) puts("4242");
    } else if (strncmp(line, "get_client_list ", 16) == 0) {
        FILE *f = fopen(clients, "r");
        char buf[64];
        while (f && fgets(buf, sizeof buf, f)) fputs(buf, stdout);
        if (f) fclose(f);
    } else if (strcmp(line, "quit") == 0) {
        close(open(quit, O_CREAT | O_WRONLY, 0600));
    } else {
        fprintf(stderr, "fake control: unknown request %s\n", line);
        rc = 1;
    }
    free(clients);
    free(quit);
    return rc;
}

static void fake_worker(int argc, char **argv) {
    mesh_fake_cuda_install(&(mesh_fake_cuda_cfg){.run = kernel});
    char *clients = pipe_file("clients");
    FILE *f = fopen(clients, "a");
    fprintf(f, "%d\n", (int)getpid());
    fclose(f);
    free(clients);
    mesh_mps_worker_main(argc, argv);
}

/* ---- the test -------------------------------------------------------------------------- */

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static const char *hello_ptx = ".version 8.8\n.target sm_121\n.address_size 64\n.visible .entry mesh_main() { ret; }\n";

static mesh_payload payload(const char *name) {
    char *toml = mesh_fmt("name = \"%s\"\nabi = 6\nview_bytes = 0x1000\nnodes = 2\ncounters = [\"tasks\"]\n", name);
    mesh_payload p = {mesh_strdup(name), toml, (uint8_t *)mesh_strdup(hello_ptx), strlen(hello_ptx)};
    return p;
}

static void install(mesh_mailbox mb, uint32_t slot, const mesh_prepared *pr, uint32_t view_off) {
    mesh_image im = {view_off, 0, pr->hash, 40, (uint32_t)pr->code_len, 6, 1};
    uint8_t e[MESH_PT_BYTES], x[MESH_PX_BYTES];
    mesh_manifest_encode_entry(&pr->manifest, &im, e);
    mesh_manifest_encode_ext(&pr->manifest, x);
    for (size_t i = 2; i < MESH_PT_BYTES / 4; i++) {
        uint32_t w;
        memcpy(&w, e + 4 * i, 4);
        mesh_mailbox_set(mb, mesh_prog_entry(slot) + 4 * i, w);
    }
    for (size_t i = 0; i < MESH_PX_BYTES / 4; i++) {
        uint32_t w;
        memcpy(&w, x + 4 * i, 4);
        mesh_mailbox_set(mb, mesh_prog_ext(slot) + 4 * i, w);
    }
    mesh_mailbox_set(mb, mesh_prog_entry(slot) + MESH_PT_STATE, MESH_ST_LOADED);
}

static void prep_ring(mesh_mailbox mb, uint32_t slot, uint32_t tail) {
    size_t c = mesh_prog_ctl(slot);
    mesh_mailbox_set(mb, c + MESH_P_RING_HEAD, tail);
    mesh_mailbox_set(mb, c + MESH_P_ONLINE, 0);
    mesh_mailbox_set(mb, c + MESH_P_HALTED, 0);
    mesh_mailbox_publish(mb, c + MESH_P_SHUTDOWN, 0);
}

static bool ticket(mesh_mailbox mb, uint32_t slot, uint32_t t, uint32_t op, uint32_t arg, uint32_t *res) {
    size_t c = mesh_prog_ctl(slot), e = c + MESH_P_RING + MESH_ENTRY_BYTES * (t % MESH_PROG_RING), k = t % MESH_PROG_RESULTS;
    mesh_mailbox_set(mb, e + MESH_E_OP, op);
    mesh_mailbox_set(mb, e + MESH_E_ARG, arg);
    mesh_mailbox_set(mb, e + MESH_E_SLOT, k);
    mesh_mailbox_publish(mb, e + MESH_E_SEQ, t + 1);
    uint64_t t0 = now_ms();
    while (mesh_mailbox_get(mb, c + MESH_P_MARKS + 4 * k) != t + 1)
        if (now_ms() - t0 > 10000) return false;
    *res = mesh_mailbox_get(mb, c + MESH_P_RESULTS + 4 * k);
    return true;
}

/* Runs one program through a backend: prepare, load, launch, tickets, halt, unload. */
static void run_one(mesh_backend *b, mesh_mailbox mb, uint32_t slot, const char *name, uint32_t view_off) {
    char *err = NULL;
    mesh_payload p = payload(name);
    mesh_prepared pr;
    const mesh_compiler *comp = b->ops->compiler(b);
    CHECK(comp->ops->needs_ptx(comp));
    if (!CHECK(comp->ops->prepare(comp, &p, NULL, "/", &pr, &err))) {
        fprintf(stderr, "prepare: %s\n", err);
        return;
    }
    CHECK(pr.exec == MESH_BACKEND_CUDA);
    install(mb, slot, &pr, view_off);
    mesh_kernel *k = NULL;
    if (!CHECK(b->ops->load(b, &pr, &k, &err))) {
        fprintf(stderr, "load: %s\n", err);
        mesh_prepared_free(&pr);
        return;
    }
    uint32_t v;
    CHECK(mesh_kernel_global_u32(k, "MESH_RT_ABI", &v, &err) && v == 6);
    CHECK(mesh_kernel_regs(k, &v, &err) && v == 40);
    CHECK(mesh_kernel_local_bytes(k, &v, &err) && v == 64);
    CHECK(mesh_kernel_max_blocks_per_sm(k, &v, &err) && v == 16);
    mesh_stream *st = NULL;
    CHECK(b->ops->new_stream(b, slot, k, &st, &err));
    uint8_t *host;
    uint64_t dev;
    CHECK(b->ops->mailbox(b, &host, &dev, &err));
    prep_ring(mb, slot, 0);
    if (!CHECK(b->ops->launch(b, st, k, 2, dev, slot, &err))) fprintf(stderr, "launch: %s\n", err);
    uint64_t t0 = now_ms();
    while (mesh_mailbox_get(mb, mesh_prog_ctl(slot) + MESH_P_ONLINE) < 2 && now_ms() - t0 < 10000) usleep(100);
    CHECK_U(mesh_mailbox_get(mb, mesh_prog_ctl(slot) + MESH_P_ONLINE), 2);
    uint32_t r;
    CHECK(ticket(mb, slot, 0, MESH_OP_INIT, 0, &r) && r == MESH_R_NONE);
    CHECK_U(mesh_mailbox_get(mb, view_off), 0x4f4c4548);
    for (uint32_t t = 1; t < 100; t++) CHECK(ticket(mb, slot, t, MESH_OP_TASK, t, &r) && r == t * 3 + 7);
    CHECK_U(mesh_mailbox_get(mb, mesh_prog_ctl(slot) + MESH_P_COUNTERS), 99);
    bool done = true;
    CHECK(b->ops->query(b, st, k, &done, &err) && !done);
    /* A halt: the nodes leave, the stream (or the worker's) goes idle. */
    mesh_mailbox_publish(mb, mesh_prog_ctl(slot) + MESH_P_SHUTDOWN, MESH_SHUTDOWN_HALT);
    t0 = now_ms();
    do {
        CHECK(b->ops->query(b, st, k, &done, &err));
    } while (!done && now_ms() - t0 < 10000);
    CHECK(done);
    CHECK_U(mesh_mailbox_get(mb, mesh_prog_ctl(slot) + MESH_P_HALTED), 2);
    mesh_stream_drop(st);
    CHECK(b->ops->unload(b, k, &err));
    mesh_prepared_free(&pr);
    free(err);
}

static void blink(mesh_shm *shm) {
    mesh_fake_cuda_install(&(mesh_fake_cuda_cfg){.run = kernel});
    mesh_nvjitlink jit;
    mesh_nvjitlink_from(&jit_fns, &jit);
    mesh_backend *b;
    char *err = NULL;
    if (!CHECK(mesh_cuda_backend_open(MESH_ISOLATION_MODE_BLINK, 0, NULL, 0, shm, &jit, &b, &err))) {
        fprintf(stderr, "open: %s\n", err);
        return;
    }
    const mesh_caps *c = b->ops->caps(b);
    CHECK_S(c->arch, "sm_121");
    CHECK(c->kind == MESH_BACKEND_CUDA && c->sms_total == 48 && c->blocks_per_sm == 24 && c->max_tg_threads == 1024);
    CHECK(!b->ops->isolated(b) && b->ops->resident(b));
    CHECK_U(b->ops->isolation(b), MESH_ISOLATION_BLINK);
    uint32_t scope;
    CHECK(!b->ops->module_scope(b, true, 3, &scope));
    CHECK(mesh_cuda_backend_context(b) != NULL);
    uint8_t *host;
    uint64_t dev;
    CHECK(b->ops->mailbox(b, &host, &dev, &err) && host == shm->base && dev == (uintptr_t)shm->base);
    run_one(b, mesh_mailbox_new(shm->base, 0), 3, "hello", 0x100000);
    mesh_fake_cuda_stats fs = mesh_fake_cuda_stats_now();
    CHECK(fs.modules_live == 0 && fs.launches == 1 && fs.last_grid == 2 && fs.last_block == 32 && fs.last_smem == 0x200);
    /* A refusal from the compiler: the node does not fit a block. */
    mesh_payload p = payload("wide");
    free(p.manifest);
    p.manifest = mesh_strdup("name = \"wide\"\nabi = 6\nshared_bytes = 0x30000\n");
    mesh_prepared pr;
    const mesh_compiler *comp = b->ops->compiler(b);
    CHECK(!comp->ops->prepare(comp, &p, NULL, "/", &pr, &err));
    CHECK_S(err, "occupancy: wide: 197120 bytes of shared memory per node (the runtime's 512 + shared_bytes); the device allows 101376 per block");
    /* And the hash a peer announced. */
    p = payload("hello");
    uint64_t wrong = 1;
    CHECK(!comp->ops->prepare(comp, &p, &wrong, "/", &pr, &err));
    CHECK(strncmp(err, "peer: hello: the PTX received hashes to ", 40) == 0);
    free(err);
    mesh_backend_destroy(b);
}

static void mps_mode(mesh_shm *shm, const char *self) {
    /* nvidia-cuda-mps-control on PATH is this binary. */
    char bin[] = "/tmp/mesh-mps-bin-XXXXXX";
    CHECK(mkdtemp(bin) != NULL);
    char *link = mesh_fmt("%s/nvidia-cuda-mps-control", bin);
    CHECK(symlink(self, link) == 0);
    char *path = mesh_fmt("%s:%s", bin, getenv("PATH") ? getenv("PATH") : "/usr/bin:/bin");
    setenv("PATH", path, 1);
    char *dir = mesh_fmt("/tmp/mesh-mps-test-%d", (int)getpid());
    mesh_fake_cuda_install(&(mesh_fake_cuda_cfg){.run = kernel});
    mesh_nvjitlink jit;
    mesh_nvjitlink_from(&jit_fns, &jit);
    mesh_backend *b;
    char *err = NULL;
    if (!CHECK(mesh_cuda_backend_open(MESH_ISOLATION_MODE_MPS, 0, dir, 1, shm, &jit, &b, &err))) {
        fprintf(stderr, "open mps: %s\n", err);
        return;
    }
    CHECK(b->ops->isolated(b) && mesh_cuda_backend_mps(b));
    CHECK_U(b->ops->isolation(b), MESH_ISOLATION_MPS);
    uint32_t scope;
    CHECK(b->ops->module_scope(b, true, 4, &scope) && scope == 4);
    CHECK(b->ops->module_scope(b, false, 0, &scope) && scope == MESH_NO_SLOT);
    char *pd = mesh_mps_native_pipe_dir(), *want = mesh_fmt("%s/pipe", dir);
    CHECK_S(pd, want);
    free(pd);
    free(want);
    /* Two programs, one after the other: the second reuses the pooled worker. */
    run_one(b, mesh_mailbox_new(shm->base, 0), 4, "hello", 0x100000);
    run_one(b, mesh_mailbox_new(shm->base, 0), 5, "again", 0x101000);
    /* The cleanup kills the workers, quits the daemon and keeps the logs of a --mps-dir. */
    mesh_backend_cleanup();
    char *ctl = mesh_fmt("%s/pipe", dir), *logs = mesh_fmt("%s/log", dir);
    CHECK(access(ctl, F_OK) != 0);
    CHECK(access(logs, F_OK) == 0);
    rmdir(logs);
    rmdir(dir);
    free(ctl);
    free(logs);
    mesh_backend_destroy(b);
    unlink(link);
    rmdir(bin);
    free(link);
    free(path);
    free(dir);
    free(err);
}

int main(int argc, char **argv) {
    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];
    if (strcmp(base, "nvidia-cuda-mps-control") == 0) return fake_control(argc, argv);
    if (argc > 1 && strcmp(argv[1], "worker") == 0) fake_worker(argc, argv);
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) return 1;
    self[n] = 0;
    setenv("CUDA_MODULE_LOADING", "EAGER", 1);
    mesh_shm shm;
    char *err = NULL;
    if (!mesh_shm_new(&shm, &err)) return 1;
    mesh_mailbox mb = mesh_mailbox_new(shm.base, 0);
    mesh_mailbox_set(mb, MESH_H_MAGIC, MESH_MAGIC);
    mesh_mailbox_set(mb, MESH_H_VERSION, MESH_VERSION);
    blink(&shm);
    mps_mode(&shm, self);
    return mesh_check_done();
}
