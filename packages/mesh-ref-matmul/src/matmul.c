/* programs/matmul's exact mode on the CPU (mesh-ref matmul.rs). */
#include "mesh/ref/matmul.h"

#include "mesh/ftz.h"
#include "mesh/ref/block.h"

#include <pthread.h>
#include <stdbool.h>

float mesh_matmul_value(uint32_t seed, uint32_t i) {
    uint32_t h = mesh_block_mix(((seed * 0x9e3779b9u) + i) * 0x85ebca6bu ^ seed);
    return mesh_ftz_sub(mesh_ftz_mul(mesh_ftz_from_u32(h >> 8), 1.0f / 8388608.0f), 1.0f);
}

void mesh_matmul_matrix(uint32_t seed, size_t n, float *out) {
    for (size_t i = 0; i < n; i++) out[i] = mesh_matmul_value(seed, (uint32_t)i);
}

typedef struct {
    const float *a, *b;
    float *c;
    size_t k, n, row0, rows;
} job;

static void *rows(void *arg) {
    const job *j = arg;
    for (size_t i = j->row0; i < j->row0 + j->rows; i++)
        for (size_t col = 0; col < j->n; col++) {
            float acc = 0.0f;
            for (size_t kk = 0; kk < j->k; kk++)
                acc = mesh_ftz_add(acc, mesh_ftz_mul(j->a[i * j->k + kk], j->b[kk * j->n + col]));
            j->c[i * j->n + col] = acc;
        }
    return NULL;
}

void mesh_matmul(const float *a, const float *b, size_t m, size_t k, size_t n, size_t threads, float *c) {
    if (threads == 0) threads = 1;
    size_t per = (m + threads - 1) / threads;
    if (per == 0) per = 1;
    size_t chunks = (m + per - 1) / per;
    if (chunks <= 1) {
        job one = {a, b, c, k, n, 0, m};
        rows(&one);
        return;
    }
    job jobs[chunks];
    pthread_t tid[chunks];
    bool started[chunks];
    for (size_t t = 0; t < chunks; t++) {
        size_t r0 = t * per;
        jobs[t] = (job){a, b, c, k, n, r0, r0 + per <= m ? per : m - r0};
        started[t] = pthread_create(&tid[t], NULL, rows, &jobs[t]) == 0;
        if (!started[t]) rows(&jobs[t]); /* no thread: do it here */
    }
    for (size_t t = 0; t < chunks; t++)
        if (started[t]) pthread_join(tid[t], NULL);
}

void mesh_matvec(const float *a, const float *x, size_t m, size_t k, float *y) {
    for (size_t i = 0; i < m; i++) {
        float lanes[32];
        for (size_t l = 0; l < 32; l++) {
            float p = 0.0f;
            for (size_t kk = l; kk < k; kk += 32) p = mesh_ftz_add(p, mesh_ftz_mul(a[i * k + kk], x[kk]));
            lanes[l] = p;
        }
        mesh_block_warp_reduce_f32(lanes, mesh_block_add_f32);
        y[i] = lanes[0];
    }
}

uint64_t mesh_matmul_digest(const float *c, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) h = (h ^ mesh_ftz_bits(c[i])) * 0x00000100000001b3ull;
    return h;
}
