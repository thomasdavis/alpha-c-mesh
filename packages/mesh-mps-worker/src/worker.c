/* mps_worker.rs. */
#include "mesh/mps_worker.h"
#include "mesh/cuda.h"
#include "mesh/log.h"
#include "mesh/mps_wire.h"
#include "mesh/text.h"

#include "mesh/abi/base.h"
#include "mesh/abi/control_block.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MPS_PIPE_ENV "CUDA_MPS_PIPE_DIRECTORY"
/* Stream polling while a kernel runs: bounds how late meshd sees a halt. */
#define POLL_US 20

static pthread_mutex_t out_mu = PTHREAD_MUTEX_INITIALIZER;

static void send_frame(uint8_t op, const uint8_t *b, size_t n) {
    pthread_mutex_lock(&out_mu);
    mesh_mps_write_frame(1, op, b, n); /* meshd gone: nothing to tell; EOF on stdin ends us */
    pthread_mutex_unlock(&out_mu);
}

/* u32 ok, then the body or the error's text. */
static void result(uint8_t op, bool ok, const uint8_t *body, size_t n, const char *err) {
    uint8_t *b = NULL;
    size_t bn = 0;
    mesh_mps_put_u32(&b, &bn, ok ? 1 : 0);
    if (ok) mesh_mps_put_bytes(&b, &bn, body, n);
    else mesh_mps_put_bytes(&b, &bn, err, strlen(err));
    send_frame(op, b, bn);
    free(b);
}

static _Noreturn void fatal(pid_t pid, const char *e) {
    mesh_error("worker %d: %s", (int)pid, e);
    mesh_log_exit(1);
}

typedef struct {
    CUcontext ctx;
    CUstream stream;
    uint32_t gen;
} watch;

static void sleep_us(unsigned us) {
    struct timespec t = {0, (long)us * 1000L};
    nanosleep(&t, NULL);
}

/* W_DONE once the stream of launch gen finished (or failed). */
static void *monitor(void *a) {
    watch *w = a;
    const mesh_cuda_fns *cu = mesh_cuda();
    cu->cuCtxSetCurrent(w->ctx);
    char *err = NULL;
    for (;;) {
        CUresult r = cu->cuStreamQuery(w->stream);
        if (r == CUDA_SUCCESS) break;
        if (r == CUDA_ERROR_NOT_READY) {
            sleep_us(POLL_US);
            continue;
        }
        char *d = mesh_cu_result_debug(r);
        err = mesh_fmt("stream: %s", d);
        free(d);
        break;
    }
    if (err) mesh_error("worker %d: %s", (int)getpid(), err);
    uint8_t *b = NULL;
    size_t bn = 0;
    mesh_mps_put_u32(&b, &bn, w->gen);
    mesh_mps_put_u32(&b, &bn, err ? 0 : 1);
    if (err) mesh_mps_put_bytes(&b, &bn, err, strlen(err));
    send_frame(MESH_W_DONE, b, bn);
    free(b);
    free(err);
    free(w);
    return NULL;
}

/* Context (green with --sms), the mailbox file registered for the device, a stream. */
static bool setup(const char *path, uint32_t sms, mesh_cuda_gpu *g, mesh_cuda_stream *st, uint64_t *dev, uint8_t **mb, char **err) {
    if (!mesh_cuda_gpu_open(sms, g, err)) return false;
    int fd = open(path, O_RDWR);
    if (fd < 0) {
        int e = errno;
        return mesh_errf(err, "mailbox %s: %s (os error %d)", path, strerror(e), e);
    }
    struct stat s;
    if (fstat(fd, &s) != 0) {
        int e = errno;
        close(fd);
        return mesh_errf(err, "%s (os error %d)", strerror(e), e);
    }
    if ((size_t)s.st_size != MESH_MAILBOX_BYTES) {
        close(fd);
        return mesh_errf(err, "mailbox %s: %lld bytes, not %d", path, (long long)s.st_size, MESH_MAILBOX_BYTES);
    }
    void *p = mmap(NULL, MESH_MAILBOX_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int e = errno;
    close(fd);
    if (p == MAP_FAILED) return mesh_errf(err, "mmap %s: %s (os error %d)", path, strerror(e), e);
    const mesh_cuda_fns *cu = mesh_cuda();
    CUdeviceptr d = 0;
    if (!mesh_cu_ck_debug(cu->cuMemHostRegister_v2(p, MESH_MAILBOX_BYTES, CU_MEMHOSTREGISTER_PORTABLE | CU_MEMHOSTREGISTER_DEVICEMAP),
                          "cuMemHostRegister", err) ||
        !mesh_cu_ck_debug(cu->cuMemHostGetDevicePointer_v2(&d, p, 0), "cuMemHostGetDevicePointer", err))
        return false;
    if (!mesh_cuda_new_stream(g, st, err)) return false;
    *dev = d;
    *mb = p;
    return true;
}

/* The module and the loader's per-image facts; the context's stack sized for it now. */
static bool load(mesh_cuda_gpu *g, const uint8_t *cubin, uint32_t threads, uint32_t smem, uint32_t scratch, mesh_cuda_kernel *k,
                 uint32_t facts[5], char **err) {
    if (!mesh_cuda_load_cubin(g, cubin, MESH_PROGRAM_ENTRY, k, err)) return false;
    size_t limit;
    bool ok = mesh_cuda_kernel_set_shape(k, threads, smem, err) && mesh_cuda_kernel_alloc_scratch(k, scratch, err) &&
              mesh_cuda_kernel_global_u32(k, MESH_RT_ABI_SYMBOL, &facts[0], err) && mesh_cuda_kernel_regs(k, &facts[1], err) &&
              mesh_cuda_kernel_max_blocks_per_sm(k, &facts[2], err) && mesh_cuda_kernel_local_bytes(k, &facts[3], err) &&
              mesh_cuda_stack_limit(g, &limit, err) && (facts[3] <= limit || mesh_cuda_set_stack_limit(g, facts[3], err));
    facts[4] = g->sms;
    if (!ok) mesh_cuda_unload(g, k, NULL);
    return ok;
}

_Noreturn void mesh_mps_worker_main(int argc, char **argv) {
    char *err = NULL;
    if (!mesh_cuda_force_eager(&err)) {
        mesh_error("worker: %s", err);
        mesh_log_exit(2);
    }
    const char *path = NULL, *sms_s = NULL;
    for (int i = 0; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--mailbox") == 0) path = argv[i + 1];
        if (strcmp(argv[i], "--sms") == 0) sms_s = argv[i + 1];
    }
    if (!path) {
        fputs("usage: meshd worker --mailbox FILE [--sms N] (started by meshd --isolation mps)\n", stderr);
        mesh_log_exit(2);
    }
    pid_t pid = getpid();
    uint32_t sms = 0;
    if (sms_s && !mesh_parse_u32(sms_s, strlen(sms_s), 10, &sms)) {
        char *q = mesh_debug_str(sms_s);
        mesh_error("worker: bad --sms %s", q);
        mesh_log_exit(2);
    }
    if (!getenv(MPS_PIPE_ENV)) {
        mesh_error("worker %d: %s is not set; refusing to run outside meshd's MPS daemon", (int)pid, MPS_PIPE_ENV);
        mesh_log_exit(2);
    }
    mesh_cuda_gpu g;
    mesh_cuda_stream st;
    uint64_t dev;
    uint8_t *mb;
    if (!setup(path, sms, &g, &st, &dev, &mb, &err)) fatal(pid, err);
    bool launched = false;
    uint32_t slot_of_launch = 0;
    bool loaded = false;
    mesh_cuda_kernel k;
    char *kname = NULL;
    uint8_t op, *b;
    size_t n;
    while (mesh_mps_read_frame(0, &op, &b, &n)) {
        if (op == MESH_W_LOAD && !loaded) {
            uint32_t threads = mesh_mps_u32_at(b, n, 0), smem = mesh_mps_u32_at(b, n, 4), scratch = mesh_mps_u32_at(b, n, 8);
            size_t nl = mesh_mps_u32_at(b, n, 12);
            if (nl > (n > 16 ? n - 16 : 0)) nl = n > 16 ? n - 16 : 0;
            char *name = mesh_strndup((const char *)b + 16, nl);
            uint32_t facts[5];
            char *e = NULL;
            if (load(&g, b + 16 + nl, threads, smem, scratch, &k, facts, &e)) {
                uint8_t *v = NULL;
                size_t vn = 0;
                for (int i = 0; i < 5; i++) mesh_mps_put_u32(&v, &vn, facts[i]);
                mesh_mps_put_u64(&v, &vn, dev);
                mesh_mps_put_u64(&v, &vn, k.scratch);
                result(MESH_W_LOADED, true, v, vn, NULL);
                free(v);
                loaded = true;
                free(kname);
                kname = name;
            } else {
                char *m = mesh_fmt("%s: %s", name, e);
                result(MESH_W_LOADED, false, NULL, 0, m);
                free(m);
                free(e);
                free(name);
            }
        } else if (op == MESH_W_LOAD) {
            result(MESH_W_LOADED, false, NULL, 0, "a worker holds one image at a time");
        } else if (op == MESH_W_UNLOAD) {
            if (loaded) {
                /* meshd unloads only a halted image: nothing of this context runs. */
                bool done = false;
                char *e = NULL;
                if (!mesh_cuda_query(&g, &st, &done, &e) || !done || !mesh_cuda_unload(&g, &k, &e)) {
                    char *m = mesh_fmt("unload %s: %s", kname, e ? e : "its kernel still runs");
                    fatal(pid, m);
                }
                loaded = false;
            }
        } else if (op == MESH_W_LAUNCH) {
            uint32_t gen = mesh_mps_u32_at(b, n, 0), slot = mesh_mps_u32_at(b, n, 4), nodes = mesh_mps_u32_at(b, n, 8);
            launched = true;
            slot_of_launch = slot;
            char *e = NULL;
            bool ok;
            if (!loaded) ok = mesh_errf(&e, "launch before load");
            else {
                bool done = false;
                ok = mesh_cuda_query(&g, &st, &done, &e);
                if (ok && !done) ok = mesh_errf(&e, "its previous kernel is still on the stream");
                if (ok) {
                    uint64_t d = dev;
                    uint32_t s = slot;
                    void *params[] = {&d, &s};
                    ok = mesh_cuda_launch_on(&g, &st, &k, nodes, params, &e);
                }
            }
            result(MESH_W_LAUNCHED, ok, NULL, 0, e);
            free(e);
            if (ok) {
                watch *w = mesh_alloc(sizeof *w);
                *w = (watch){g.ctx, st.s, gen};
                pthread_t t;
                if (pthread_create(&t, NULL, monitor, w) == 0) pthread_detach(t);
            }
        } else if (op == MESH_W_READY) {
            result(MESH_W_READY, true, NULL, 0, NULL);
        } else if (op == MESH_W_QUIT) {
            free(b);
            break;
        } else {
            char *m = mesh_fmt("unknown request %u", op);
            fatal(pid, m);
        }
        free(b);
    }
    /* W_QUIT comes only once meshd halted us; EOF may mean meshd died with our
     * kernel resident, and nobody else will stop it: P_SHUTDOWN ourselves. */
    bool done = true;
    char *e = NULL;
    if (launched && mesh_cuda_query(&g, &st, &done, &e) && !done) {
        size_t at = mesh_prog_ctl(slot_of_launch) + MESH_P_SHUTDOWN;
        if (at < MESH_MAILBOX_BYTES) atomic_store_explicit((_Atomic uint32_t *)(void *)(mb + at), 1u, memory_order_release);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (;;) {
            done = true;
            if (!mesh_cuda_query(&g, &st, &done, NULL) || done) break;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (t1.tv_sec - t0.tv_sec >= 2) break;
            sleep_us(POLL_US);
        }
    }
    free(e);
    /* Still running: exit without the unload, which would wait for it forever (the exit frees the context). */
    done = false;
    if (loaded && mesh_cuda_query(&g, &st, &done, NULL) && done) mesh_cuda_unload(&g, &k, NULL);
    free(kname);
    mesh_cuda_gpu_free(&g);
    mesh_log_exit(0);
}
