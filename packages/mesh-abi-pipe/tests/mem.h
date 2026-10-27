/* A word-addressed memory of C11 atomics behind mesh_words: the tests' mailbox,
 * as Rust's tests use a Vec<AtomicU32>. */
#ifndef MESH_TEST_MEM_H
#define MESH_TEST_MEM_H

#include "mesh/abi/words.h"

#include <stdatomic.h>
#include <stdlib.h>

typedef struct {
    _Atomic uint32_t *w;
    size_t n;
    mesh_words words;
} mem;

static uint32_t mem_load(void *c, size_t a) { return atomic_load_explicit(&((mem *)c)->w[a / 4], memory_order_acquire); }
static void mem_store(void *c, size_t a, uint32_t v) { atomic_store_explicit(&((mem *)c)->w[a / 4], v, memory_order_release); }
static uint32_t mem_cas(void *c, size_t a, uint32_t cmp, uint32_t desired) {
    uint32_t seen = cmp;
    atomic_compare_exchange_strong_explicit(&((mem *)c)->w[a / 4], &seen, desired, memory_order_acq_rel, memory_order_acquire);
    return seen;
}
static uint32_t mem_add(void *c, size_t a, uint32_t v) { return atomic_fetch_add_explicit(&((mem *)c)->w[a / 4], v, memory_order_acq_rel); }
static void mem_read(void *c, size_t a, uint8_t *out, size_t n) {
    for (size_t i = 0; 4 * i < n; i++) {
        uint32_t v = atomic_load_explicit(&((mem *)c)->w[a / 4 + i], memory_order_relaxed);
        for (size_t k = 0; k < 4 && 4 * i + k < n; k++) out[4 * i + k] = (uint8_t)(v >> (8 * k));
    }
}
static void mem_write(void *c, size_t a, const uint8_t *b, size_t n) {
    for (size_t i = 0; 4 * i < n; i++) {
        uint32_t v = 0;
        for (size_t k = 0; k < 4 && 4 * i + k < n; k++) v |= (uint32_t)b[4 * i + k] << (8 * k);
        atomic_store_explicit(&((mem *)c)->w[a / 4 + i], v, memory_order_relaxed);
    }
}

static inline mem *mem_new(size_t bytes) {
    mem *m = calloc(1, sizeof *m);
    m->n = bytes / 4;
    m->w = calloc(m->n, sizeof *m->w);
    m->words = (mesh_words){m, mem_load, mem_store, mem_cas, mem_add, NULL, mem_read, mem_write};
    return m;
}
static inline void mem_free(mem *m) {
    free((void *)m->w);
    free(m);
}
/* Plain (relaxed) word access for setting up and inspecting. */
static inline void mem_set(mem *m, size_t a, uint32_t v) { atomic_store_explicit(&m->w[a / 4], v, memory_order_relaxed); }
static inline uint32_t mem_get(mem *m, size_t a) { return atomic_load_explicit(&m->w[a / 4], memory_order_relaxed); }

/* Writes position p's record (words v[0..n)) and its sum, as a producer would. */
static inline void mem_put(mem *m, const mesh_pipe_ring *r, uint32_t p, const uint32_t *v, size_t n) {
    uint8_t b[4096];
    for (size_t i = 0; i < n; i++) {
        mem_set(m, mesh_pipe_ring_cell(r, p) + 4 * i, v[i]);
        for (int k = 0; k < 4; k++) b[4 * i + (size_t)k] = (uint8_t)(v[i] >> (8 * k));
    }
    mem_set(m, mesh_pipe_ring_sum(r, p), mesh_pipe_rec_sum_at(b, 4 * n, p));
}
static inline uint32_t mem_cell_word(mem *m, const mesh_pipe_ring *r, uint32_t p, size_t i) {
    return mem_get(m, mesh_pipe_ring_cell(r, p) + 4 * i);
}

#endif
