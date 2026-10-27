/* @ajax/mesh-abi-words: Word and byte access to the mailbox at mailbox offsets: the interface pipes run over.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_WORDS_H
#define MESH_ABI_WORDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/* Word access to the mailbox, at mailbox offsets (every runtime maps the
 * offsets to its own addresses): Rust's `Words` trait (and `Bytes`, its byte
 * copies for the host runtimes), as a table of functions over a context.
 * A GPU runtime fills it with its own atomics; static inline callers given a
 * constant table are devirtualised by the compiler. */
typedef struct mesh_words {
    void *ctx;
    uint32_t (*load_acquire)(void *ctx, size_t a);
    void (*store_release)(void *ctx, size_t a, uint32_t v);
    /* Returns the old value. */
    uint32_t (*cas)(void *ctx, size_t a, uint32_t cmp, uint32_t desired);
    /* Returns the old value. */
    uint32_t (*add)(void *ctx, size_t a, uint32_t v);
    /* A sequentially consistent fence (a store before it is ordered before a
     * load after it). NULL: atomic_thread_fence(memory_order_seq_cst). */
    void (*fence)(void *ctx);
    /* Bytes: copies for the host runtimes (wasm lanes, native workers); a GPU
     * warp copies with all its lanes between reserve and commit instead. NULL
     * where only words are used. */
    void (*read)(void *ctx, size_t a, uint8_t *out, size_t n);
    void (*write)(void *ctx, size_t a, const uint8_t *b, size_t n);
} mesh_words;

static inline uint32_t mesh_words_load(const mesh_words *w, size_t a) { return w->load_acquire(w->ctx, a); }
static inline void mesh_words_store(const mesh_words *w, size_t a, uint32_t v) { w->store_release(w->ctx, a, v); }
static inline uint32_t mesh_words_cas(const mesh_words *w, size_t a, uint32_t cmp, uint32_t desired) {
    return w->cas(w->ctx, a, cmp, desired);
}
static inline uint32_t mesh_words_add(const mesh_words *w, size_t a, uint32_t v) { return w->add(w->ctx, a, v); }
static inline void mesh_words_fence(const mesh_words *w) {
    if (w->fence) w->fence(w->ctx);
    else atomic_thread_fence(memory_order_seq_cst);
}
static inline void mesh_words_read(const mesh_words *w, size_t a, uint8_t *out, size_t n) { w->read(w->ctx, a, out, n); }
static inline void mesh_words_write(const mesh_words *w, size_t a, const uint8_t *b, size_t n) { w->write(w->ctx, a, b, n); }

#endif
