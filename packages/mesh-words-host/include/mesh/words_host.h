/* @ajax/mesh-words-host: mesh_words over a host mapping of the mailbox (or any
 * 4-byte-aligned memory), with C11 atomics: loads acquire, stores release,
 * read-modify-writes acq_rel, byte copies relaxed word by word (little-endian),
 * as the Rust host runtimes and tests access the mailbox. Header-only. */
#ifndef MESH_WORDS_HOST_H
#define MESH_WORDS_HOST_H

#include "mesh/abi/words.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

static inline _Atomic uint32_t *mesh_words_host_at_(void *base, size_t a) { return (_Atomic uint32_t *)base + a / 4; }

static inline uint32_t mesh_words_host_load_(void *c, size_t a) {
    return atomic_load_explicit(mesh_words_host_at_(c, a), memory_order_acquire);
}
static inline void mesh_words_host_store_(void *c, size_t a, uint32_t v) {
    atomic_store_explicit(mesh_words_host_at_(c, a), v, memory_order_release);
}
static inline uint32_t mesh_words_host_cas_(void *c, size_t a, uint32_t cmp, uint32_t desired) {
    uint32_t seen = cmp;
    atomic_compare_exchange_strong_explicit(mesh_words_host_at_(c, a), &seen, desired, memory_order_acq_rel,
                                            memory_order_acquire);
    return seen;
}
static inline uint32_t mesh_words_host_add_(void *c, size_t a, uint32_t v) {
    return atomic_fetch_add_explicit(mesh_words_host_at_(c, a), v, memory_order_acq_rel);
}
static inline void mesh_words_host_read_(void *c, size_t a, uint8_t *out, size_t n) {
    for (size_t i = 0; 4 * i < n; i++) {
        uint32_t v = atomic_load_explicit(mesh_words_host_at_(c, a + 4 * i), memory_order_relaxed);
        for (size_t k = 0; k < 4 && 4 * i + k < n; k++) out[4 * i + k] = (uint8_t)(v >> (8 * k));
    }
}
static inline void mesh_words_host_write_(void *c, size_t a, const uint8_t *b, size_t n) {
    for (size_t i = 0; 4 * i < n; i++) {
        uint32_t v = 0;
        for (size_t k = 0; k < 4 && 4 * i + k < n; k++) v |= (uint32_t)b[4 * i + k] << (8 * k);
        atomic_store_explicit(mesh_words_host_at_(c, a + 4 * i), v, memory_order_relaxed);
    }
}

/* mesh_words over base: offset a is the word at base + a (a multiple of 4).
 * base must be 4-byte aligned and outlive the table. */
static inline mesh_words mesh_words_host(void *base) {
    mesh_words w = {base, mesh_words_host_load_, mesh_words_host_store_, mesh_words_host_cas_, mesh_words_host_add_,
                    NULL, mesh_words_host_read_, mesh_words_host_write_};
    return w;
}

#endif
