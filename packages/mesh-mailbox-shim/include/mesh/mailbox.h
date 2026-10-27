/* @ajax/mesh-mailbox-shim: a private stand-in (never published) for exec's
 * mailbox package, so the daemon compiles and tests before it lands.
 *
 * Host views of the mailbox words (mailbox.rs): the GPU writes the same
 * memory concurrently, so every access is an atomic of the word's size:
 * Release stores publish, Acquire loads observe completion. */
#ifndef MESH_MAILBOX_H
#define MESH_MAILBOX_H

#include "mesh/abi/base.h"
#include "mesh/abi/words.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint8_t *base;
    uint64_t dev; /* the device address the kernels get */
} mesh_mailbox;

static inline void mesh_mailbox_check_(size_t off, size_t align) {
    if (off % align != 0 || off + align > MESH_MAILBOX_BYTES) {
        fprintf(stderr, "mailbox offset %#zx\n", off);
        abort();
    }
}
static inline _Atomic uint32_t *mesh_mailbox_a32_(const mesh_mailbox *mb, size_t off) {
    mesh_mailbox_check_(off, 4);
    return (_Atomic uint32_t *)(void *)(mb->base + off);
}
static inline _Atomic uint64_t *mesh_mailbox_a64_(const mesh_mailbox *mb, size_t off) {
    mesh_mailbox_check_(off, 8);
    return (_Atomic uint64_t *)(void *)(mb->base + off);
}
/* Acquire load. */
static inline uint32_t mesh_mailbox_get(const mesh_mailbox *mb, size_t off) {
    return atomic_load_explicit(mesh_mailbox_a32_(mb, off), memory_order_acquire);
}
/* Payload store (ordered before the next publish). */
static inline void mesh_mailbox_set(const mesh_mailbox *mb, size_t off, uint32_t v) {
    atomic_store_explicit(mesh_mailbox_a32_(mb, off), v, memory_order_relaxed);
}
/* Release store. */
static inline void mesh_mailbox_publish(const mesh_mailbox *mb, size_t off, uint32_t v) {
    atomic_store_explicit(mesh_mailbox_a32_(mb, off), v, memory_order_release);
}
static inline uint32_t mesh_mailbox_fetch_add(const mesh_mailbox *mb, size_t off, uint32_t v) {
    return atomic_fetch_add_explicit(mesh_mailbox_a32_(mb, off), v, memory_order_acq_rel);
}
/* Compare-and-swap (AcqRel); returns the value seen (== cur on success). */
static inline uint32_t mesh_mailbox_cas(const mesh_mailbox *mb, size_t off, uint32_t cur, uint32_t next) {
    atomic_compare_exchange_strong_explicit(mesh_mailbox_a32_(mb, off), &cur, next, memory_order_acq_rel, memory_order_acquire);
    return cur;
}
static inline uint64_t mesh_mailbox_fetch_add64(const mesh_mailbox *mb, size_t off, uint64_t v) {
    return atomic_fetch_add_explicit(mesh_mailbox_a64_(mb, off), v, memory_order_acq_rel);
}
static inline uint64_t mesh_mailbox_get64(const mesh_mailbox *mb, size_t off) {
    return atomic_load_explicit(mesh_mailbox_a64_(mb, off), memory_order_acquire);
}
static inline void mesh_mailbox_set64(const mesh_mailbox *mb, size_t off, uint64_t v) {
    atomic_store_explicit(mesh_mailbox_a64_(mb, off), v, memory_order_relaxed);
}
static inline void mesh_mailbox_publish64(const mesh_mailbox *mb, size_t off, uint64_t v) {
    atomic_store_explicit(mesh_mailbox_a64_(mb, off), v, memory_order_release);
}
/* len bytes at off (word reads; off 4-aligned). */
static inline void mesh_mailbox_read(const mesh_mailbox *mb, size_t off, void *out, size_t len) {
    uint8_t *o = out;
    for (size_t w = 0; w < len; w += 4) {
        uint32_t v = atomic_load_explicit(mesh_mailbox_a32_(mb, off + w), memory_order_relaxed);
        for (size_t i = 0; i < 4 && w + i < len; i++) o[w + i] = (uint8_t)(v >> (8 * i));
    }
}
/* Writes n bytes at off, zero padding to span bytes (word stores). */
static inline void mesh_mailbox_write(const mesh_mailbox *mb, size_t off, const void *bytes, size_t n, size_t span) {
    const uint8_t *b = bytes;
    for (size_t w = 0; w < span; w += 4) {
        uint32_t v = 0;
        for (size_t i = 0; i < 4; i++) if (w + i < n) v |= (uint32_t)b[w + i] << (8 * i);
        mesh_mailbox_set(mb, off + w, v);
    }
}
static inline void mesh_mailbox_zero(const mesh_mailbox *mb, size_t off, size_t len) { mesh_mailbox_write(mb, off, NULL, 0, len); }

/* mesh_words over the mailbox (pipes from host code). */
static inline uint32_t mesh_mailbox_w_load_(void *c, size_t a) { return mesh_mailbox_get(c, a); }
static inline void mesh_mailbox_w_store_(void *c, size_t a, uint32_t v) { mesh_mailbox_publish(c, a, v); }
static inline uint32_t mesh_mailbox_w_cas_(void *c, size_t a, uint32_t x, uint32_t y) { return mesh_mailbox_cas(c, a, x, y); }
static inline uint32_t mesh_mailbox_w_add_(void *c, size_t a, uint32_t v) { return mesh_mailbox_fetch_add(c, a, v); }
static inline void mesh_mailbox_w_read_(void *c, size_t a, uint8_t *out, size_t n) { mesh_mailbox_read(c, a, out, n); }
static inline void mesh_mailbox_w_write_(void *c, size_t a, const uint8_t *b, size_t n) {
    for (size_t i = 0; 4 * i < n; i++) {
        uint32_t v = 0;
        for (size_t k = 0; k < 4 && 4 * i + k < n; k++) v |= (uint32_t)b[4 * i + k] << (8 * k);
        mesh_mailbox_set(c, a + 4 * i, v);
    }
}
static inline mesh_words mesh_mailbox_words(const mesh_mailbox *mb) {
    mesh_words w = {(void *)mb, mesh_mailbox_w_load_, mesh_mailbox_w_store_, mesh_mailbox_w_cas_, mesh_mailbox_w_add_,
                    NULL, mesh_mailbox_w_read_, mesh_mailbox_w_write_};
    return w;
}

/* A heap mailbox for tests (free(base) after). */
static inline mesh_mailbox mesh_mailbox_test(void) {
    mesh_mailbox mb = {(uint8_t *)aligned_alloc(4096, MESH_MAILBOX_BYTES), 0};
    for (size_t i = 0; i < MESH_MAILBOX_BYTES; i++) mb.base[i] = 0;
    return mb;
}

#endif
