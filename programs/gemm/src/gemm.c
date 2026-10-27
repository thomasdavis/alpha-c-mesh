/* gemm: f32 matrix multiply throughput as a native worker program: a
 * cache-blocked GEMM (4 x 16 register tiles, f32 fused multiply-add) over
 * every CPU this process may use.
 *
 * View (fields in program.toml): 0x00 magic, 0x04 size (the periodic task's
 * n), 0x08 last n, 0x0c last us, 0x10 last GFLOP/s, 0x14 best, 0x18 plain
 * loop GFLOP/s, 0x1c fast path on one thread, 0x20 impl, 0x24 threads, 0x28
 * runs, 0x1000.. C of the last check (n <= 64). Jobs: each item (OP_JOB) is
 * one GEMM of the view's size (`setup 7:<n>` sets it), its result the
 * microseconds it took and its job_out record (n, GFLOP/s).
 * Ported from alpha-rust-mesh programs/gemm/src/main.rs (its Linux path). */
#include "gemm.h"

#include "mesh/abi/ops.h"
#include "mesh/abi/ring.h"
#include "mesh/ref/gemm.h"

#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define KB 256u
#define JB 512u

static size_t min_z(size_t a, size_t b) { return a < b ? a : b; }

static void tile(size_t n, const float *a, const float *b, float *c, size_t r0, size_t i, size_t j, size_t k0, size_t k1) {
    float acc[4][16];
    for (size_t r = 0; r < 4; r++) memcpy(acc[r], c + (i + r) * n + j, sizeof acc[r]);
    for (size_t k = k0; k < k1; k++) {
        const float *bk = b + k * n + j;
        for (size_t r = 0; r < 4; r++) {
            float x = a[(r0 + i + r) * n + k];
            for (size_t t = 0; t < 16; t++) acc[r][t] = fmaf(x, bk[t], acc[r][t]);
        }
    }
    for (size_t r = 0; r < 4; r++) memcpy(c + (i + r) * n + j, acc[r], sizeof acc[r]);
}

static void edge(size_t n, const float *a, const float *b, float *c, size_t r0, size_t i, size_t ir, size_t j, size_t jw, size_t k0, size_t k1) {
    for (size_t r = 0; r < ir; r++)
        for (size_t k = k0; k < k1; k++) {
            float x = a[(r0 + i + r) * n + k];
            for (size_t t = 0; t < jw; t++) c[(i + r) * n + j + t] = fmaf(x, b[k * n + j + t], c[(i + r) * n + j + t]);
        }
}

/* Rows [r0, r0 + rows) of C = A B: k blocks of KB, j blocks of JB, 4 x 16 tiles. */
static void rows(size_t n, const float *a, const float *b, float *c, size_t rows_n, size_t r0) {
    memset(c, 0, sizeof(float) * rows_n * n);
    for (size_t k0 = 0; k0 < n; k0 += KB) {
        size_t k1 = min_z(k0 + KB, n);
        for (size_t j0 = 0; j0 < n; j0 += JB) {
            size_t j1 = min_z(j0 + JB, n);
            for (size_t i = 0; i < rows_n;) {
                size_t ir = min_z(rows_n - i, 4);
                for (size_t j = j0; j < j1;) {
                    size_t jw = min_z(j1 - j, 16);
                    if (ir == 4 && jw == 16) tile(n, a, b, c, r0, i, j, k0, k1);
                    else edge(n, a, b, c, r0, i, ir, j, jw, k0, k1);
                    j += jw;
                }
                i += ir;
            }
        }
    }
}

static _Atomic size_t threads_set;

size_t gemm_threads(void) {
    size_t t = atomic_load(&threads_set);
    if (t) return t;
    long c = sysconf(_SC_NPROCESSORS_ONLN);
    return c > 0 ? (size_t)c : 1;
}

typedef struct {
    size_t n, r0, rows;
    const float *a, *b;
    float *c;
    pthread_t th;
} chunk;

static void *run_chunk(void *arg) {
    chunk *k = arg;
    rows(k->n, k->a, k->b, k->c + k->r0 * k->n, k->rows, k->r0);
    return NULL;
}

void gemm_fast(size_t n, const float *a, const float *b, float *c, bool single) {
    size_t t = single ? 1 : gemm_threads();
    if (!single) {
        size_t cap = (n + 3) / 4;
        if (t > cap) t = cap;
        if (t < 1) t = 1;
    }
    size_t per = ((n + t - 1) / t + 3) / 4 * 4;
    size_t nchunks = (n + per - 1) / per;
    chunk *ks = calloc(nchunks, sizeof *ks);
    if (!ks) return;
    for (size_t i = 0; i < nchunks; i++) {
        size_t r0 = i * per;
        ks[i] = (chunk){n, r0, min_z(per, n - r0), a, b, c, 0};
        if (pthread_create(&ks[i].th, NULL, run_chunk, &ks[i]) != 0) run_chunk(&ks[i]), ks[i].rows = 0;
    }
    for (size_t i = 0; i < nchunks; i++)
        if (ks[i].rows) pthread_join(ks[i].th, NULL);
    free(ks);
}

void gemm_plain(size_t n, const float *a, const float *b, float *c) {
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) {
            float s = 0.0f;
            for (size_t k = 0; k < n; k++) s += a[i * n + k] * b[k * n + j];
            c[i * n + j] = s;
        }
}

/* The size, A, B and C of the last GEMM (per node thread), freed when the
 * thread ends, as Rust's thread_local is dropped. */
typedef struct {
    size_t n;
    float *a, *b, *c;
} bufs;
static pthread_key_t buf_key;
static pthread_once_t buf_once = PTHREAD_ONCE_INIT;

static void free_bufs(void *p) {
    bufs *b = p;
    free(b->a), free(b->b), free(b->c);
    free(b);
}
static void make_key(void) { pthread_key_create(&buf_key, free_bufs); }

/* This thread's buffers holding size n's inputs; NULL when memory runs out. */
static bufs *with_buf(size_t n) {
    pthread_once(&buf_once, make_key);
    bufs *b = pthread_getspecific(buf_key);
    if (!b) {
        b = calloc(1, sizeof *b);
        if (!b || pthread_setspecific(buf_key, b) != 0) { free(b); return NULL; }
    }
    if (b->n == n) return b;
    free(b->a), free(b->b), free(b->c);
    b->n = 0;
    b->a = malloc(sizeof(float) * n * n);
    b->b = malloc(sizeof(float) * n * n);
    b->c = calloc(n * n, sizeof(float));
    if (!b->a || !b->b || !b->c) {
        free(b->a), free(b->b), free(b->c);
        b->a = b->b = b->c = NULL;
        return NULL;
    }
    mesh_gemm_inputs(n, b->a, b->b);
    b->n = n;
    return b;
}

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}
static float gflops(size_t n, double secs) { return (float)(2.0 * (double)n * (double)n * (double)n / secs / 1e9); }

void gemm_init(const mesh_worker_ctx *ctx, void *user) {
    (void)user;
    mesh_ctx_set_u32(ctx, 0, GEMM_MAGIC);
    mesh_ctx_set_u32(ctx, GEMM_V_SIZE, mesh_ctx_param(ctx) != 0 ? mesh_ctx_param(ctx) : 1024);
    mesh_ctx_set_u32(ctx, GEMM_V_IMPL, GEMM_IMPL);
    mesh_ctx_set_u32(ctx, GEMM_V_THREADS, (uint32_t)gemm_threads());
}

static bool size(const mesh_worker_ctx *ctx, uint32_t arg, size_t *n) {
    uint32_t v = arg != 0 ? arg : mesh_ctx_view_u32(ctx, GEMM_V_SIZE);
    *n = v;
    return v >= 1 && v <= GEMM_N_MAX;
}

/* One fast GEMM of size n; its seconds, or a negative number when memory ran out. */
static double timed(size_t n, bool single, bool plain, bufs **used) {
    bufs *b = with_buf(n);
    if (!b) return -1;
    if (used) *used = b;
    double t0 = now_s();
    if (plain) gemm_plain(n, b->a, b->b, b->c);
    else gemm_fast(n, b->a, b->b, b->c, single);
    return now_s() - t0;
}

static void record(const mesh_worker_ctx *ctx, size_t n, double secs, float g) {
    mesh_ctx_set_u32(ctx, GEMM_V_N, (uint32_t)n);
    mesh_ctx_set_u32(ctx, GEMM_V_US, (uint32_t)(secs * 1e6));
    mesh_ctx_set_f32(ctx, GEMM_V_GFLOPS, g);
    if (g > mesh_ctx_view_f32(ctx, GEMM_V_BEST)) mesh_ctx_set_f32(ctx, GEMM_V_BEST, g);
    mesh_ctx_set_u32(ctx, GEMM_V_RUNS, mesh_ctx_view_u32(ctx, GEMM_V_RUNS) + 1);
    mesh_ctx_count(ctx, 0, 1);
    mesh_ctx_count(ctx, 1, (uint32_t)llround(2.0 * (double)n * (double)n * (double)n / 1e9));
}

uint32_t gemm_task(const mesh_worker_ctx *ctx, uint32_t op, uint32_t arg, void *user) {
    (void)user;
    size_t n;
    switch (op) {
    case MESH_OP_JOB: {
        if (!size(ctx, 0, &n)) return MESH_R_JOB_FAIL;
        double secs = timed(n, false, false, NULL);
        if (secs < 0) return MESH_R_JOB_FAIL;
        float g = gflops(n, secs);
        uint32_t rec = GEMM_V_JOB_OUT + 8 * (mesh_ctx_ticket(ctx) % MESH_PROG_RESULTS);
        mesh_ctx_set_u32(ctx, rec, (uint32_t)n);
        mesh_ctx_set_f32(ctx, rec + 4, g);
        record(ctx, n, secs, g);
        uint32_t us = (uint32_t)(secs * 1e6);
        return us < MESH_R_JOB_FAIL - 1 ? us : MESH_R_JOB_FAIL - 1;
    }
    case GEMM_OP_SIZE:
        if (!size(ctx, arg, &n)) return MESH_R_BAD_OP;
        mesh_ctx_set_u32(ctx, GEMM_V_SIZE, (uint32_t)n);
        return (uint32_t)n;
    case MESH_OP_TASK: {
        if (!size(ctx, arg, &n)) return MESH_R_BAD_OP;
        double secs = timed(n, false, false, NULL);
        if (secs < 0) return MESH_R_BAD_OP;
        record(ctx, n, secs, gflops(n, secs));
        return (uint32_t)(secs * 1e6);
    }
    case GEMM_OP_CHECK: {
        n = arg;
        bufs *b = NULL;
        if (n < 1 || n > GEMM_CHECK_MAX || timed(n, false, false, &b) < 0) return MESH_R_BAD_OP;
        mesh_view_write_f32s(mesh_ctx_view(ctx), GEMM_V_C, b->c, n * n);
        mesh_ctx_count(ctx, 3, 1);
        return (uint32_t)n;
    }
    case GEMM_OP_PLAIN:
    case GEMM_OP_SINGLE: {
        if (!size(ctx, arg, &n)) return MESH_R_BAD_OP;
        double secs = timed(n, op == GEMM_OP_SINGLE, op == GEMM_OP_PLAIN, NULL);
        if (secs < 0) return MESH_R_BAD_OP;
        mesh_ctx_set_f32(ctx, op == GEMM_OP_PLAIN ? GEMM_V_PLAIN : GEMM_V_SINGLE, gflops(n, secs));
        if (op == GEMM_OP_PLAIN) mesh_ctx_count(ctx, 2, 1);
        return (uint32_t)(secs * 1e6);
    }
    case GEMM_OP_THREADS:
        atomic_store(&threads_set, (size_t)arg);
        mesh_ctx_set_u32(ctx, GEMM_V_THREADS, (uint32_t)gemm_threads());
        return (uint32_t)gemm_threads();
    default: return MESH_R_BAD_OP;
    }
}
