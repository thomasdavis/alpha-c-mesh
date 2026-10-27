/* programs/pathtrace's CPU reference (mesh-ref pathtrace.rs): tiles in Z, the raster, the PPM. */
#include "mesh/ref/pathtrace.h"

#include "pathtrace/z.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool mesh_pt_tile_render(const uint32_t *sc, uint32_t t, uint32_t passes, uint32_t op, mesh_pt_tile *out) {
    memset(out, 0, sizeof *out);
    out->t = t;
    out->passes = passes;
    out->part_digests = calloc(passes ? passes : 1, sizeof *out->part_digests);
    pt_v *acc = calloc(PT_TILE_PIX, sizeof *acc), *part = calloc(PT_TILE_PIX, sizeof *part);
    if (!out->part_digests || !acc || !part) {
        free(out->part_digests), free(acc), free(part);
        out->part_digests = NULL;
        return false;
    }
    for (uint32_t p = 0; p < passes; p++) {
        out->part_digests[p] = pt_tile_pass_z(sc, t, p, part);
        pt_tile_fold_z(sc, p + 1, op, acc, part, out->rgb, &out->acc_digest, &out->rgb_digest);
    }
    for (uint32_t i = 0; i < PT_TILE_PIX; i++) {
        out->acc[3 * i] = pt_to_bits(acc[i].x);
        out->acc[3 * i + 1] = pt_to_bits(acc[i].y);
        out->acc[3 * i + 2] = pt_to_bits(acc[i].z);
    }
    free(acc), free(part);
    return true;
}

void mesh_pt_tile_free(mesh_pt_tile *tile) {
    free(tile->part_digests);
    tile->part_digests = NULL;
}

typedef struct {
    const uint32_t *sc, *ts;
    size_t n;
    uint32_t passes, op;
    mesh_pt_tile *out;
    atomic_size_t next;
    atomic_bool failed;
} work;

static void *worker(void *arg) {
    work *w = arg;
    for (;;) {
        size_t i = atomic_fetch_add(&w->next, 1);
        if (i >= w->n) return NULL;
        if (!mesh_pt_tile_render(w->sc, w->ts[i], w->passes, w->op, &w->out[i])) atomic_store(&w->failed, true);
    }
}

bool mesh_pt_tiles(const uint32_t *sc, const uint32_t *ts, size_t n, uint32_t passes, uint32_t op, mesh_pt_tile *out) {
    work w = {sc, ts, n, passes, op, out, 0, false};
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    size_t threads = cpus > 0 ? (size_t)cpus : 4;
    if (threads > n) threads = n ? n : 1;
    pthread_t tid[threads];
    size_t started = 0;
    for (size_t i = 0; i < threads; i++)
        if (pthread_create(&tid[started], NULL, worker, &w) == 0) started++;
    if (started == 0) worker(&w);
    for (size_t i = 0; i < started; i++) pthread_join(tid[i], NULL);
    if (atomic_load(&w.failed)) {
        for (size_t i = 0; i < n; i++) mesh_pt_tile_free(&out[i]);
        return false;
    }
    return true;
}

void mesh_pt_raster(const uint32_t *sc, const uint32_t *rgb, uint8_t *out) {
    uint32_t w = sc[PT_H_WIDTH], h = sc[PT_H_HEIGHT], tx = w / PT_TILE_W;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t t = (y / PT_TILE_H) * tx + x / PT_TILE_W;
            uint32_t p = rgb[t * PT_TILE_PIX + (y % PT_TILE_H) * PT_TILE_W + x % PT_TILE_W];
            size_t o = 3 * ((size_t)y * w + x);
            out[o] = (uint8_t)p;
            out[o + 1] = (uint8_t)(p >> 8);
            out[o + 2] = (uint8_t)(p >> 16);
        }
}

uint8_t *mesh_pt_ppm(uint32_t w, uint32_t h, const uint8_t *raster, size_t *len) {
    char head[64];
    int n = snprintf(head, sizeof head, "P6\n%u %u\n255\n", w, h);
    size_t body = (size_t)w * h * 3;
    uint8_t *b = malloc((size_t)n + body);
    if (!b) return NULL;
    memcpy(b, head, (size_t)n);
    memcpy(b + n, raster, body);
    *len = (size_t)n + body;
    return b;
}
