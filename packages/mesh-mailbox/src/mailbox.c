/* mailbox.rs: checked atomic word, byte and window access. */
#include "mesh/mailbox.h"
#include "mesh/words_host.h"

#include "mesh/abi/base.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Noreturn void bad(size_t off) {
    fprintf(stderr, "mailbox offset 0x%zx\n", off);
    abort();
}

static _Atomic uint32_t *a32(mesh_mailbox m, size_t off) {
    if (off % 4 != 0 || off + 4 > MESH_MAILBOX_BYTES) bad(off);
    return (_Atomic uint32_t *)(void *)(m.base + off);
}

static _Atomic uint64_t *a64(mesh_mailbox m, size_t off) {
    if (off % 8 != 0 || off + 8 > MESH_MAILBOX_BYTES) bad(off);
    return (_Atomic uint64_t *)(void *)(m.base + off);
}

uint32_t mesh_mailbox_get(mesh_mailbox m, size_t off) { return atomic_load_explicit(a32(m, off), memory_order_acquire); }
void mesh_mailbox_set(mesh_mailbox m, size_t off, uint32_t v) { atomic_store_explicit(a32(m, off), v, memory_order_relaxed); }
void mesh_mailbox_publish(mesh_mailbox m, size_t off, uint32_t v) { atomic_store_explicit(a32(m, off), v, memory_order_release); }
uint32_t mesh_mailbox_fetch_add(mesh_mailbox m, size_t off, uint32_t v) {
    return atomic_fetch_add_explicit(a32(m, off), v, memory_order_acq_rel);
}
uint32_t mesh_mailbox_cas(mesh_mailbox m, size_t off, uint32_t cur, uint32_t desired) {
    uint32_t seen = cur;
    atomic_compare_exchange_strong_explicit(a32(m, off), &seen, desired, memory_order_acq_rel, memory_order_acquire);
    return seen;
}
uint64_t mesh_mailbox_fetch_add64(mesh_mailbox m, size_t off, uint64_t v) {
    return atomic_fetch_add_explicit(a64(m, off), v, memory_order_acq_rel);
}
uint8_t mesh_mailbox_get8(mesh_mailbox m, size_t off) {
    if (off >= MESH_MAILBOX_BYTES) bad(off);
    return atomic_load_explicit((_Atomic uint8_t *)(void *)(m.base + off), memory_order_acquire);
}
void mesh_mailbox_set8(mesh_mailbox m, size_t off, uint8_t v) {
    if (off >= MESH_MAILBOX_BYTES) bad(off);
    atomic_store_explicit((_Atomic uint8_t *)(void *)(m.base + off), v, memory_order_release);
}
uint64_t mesh_mailbox_get64(mesh_mailbox m, size_t off) { return atomic_load_explicit(a64(m, off), memory_order_acquire); }
void mesh_mailbox_set64(mesh_mailbox m, size_t off, uint64_t v) { atomic_store_explicit(a64(m, off), v, memory_order_relaxed); }
void mesh_mailbox_publish64(mesh_mailbox m, size_t off, uint64_t v) { atomic_store_explicit(a64(m, off), v, memory_order_release); }

void mesh_mailbox_read(mesh_mailbox m, size_t off, uint8_t *out, size_t len) {
    for (size_t w = 0; w < len; w += 4) {
        uint32_t v = atomic_load_explicit(a32(m, off + w), memory_order_relaxed);
        for (size_t i = 0; i < 4 && w + i < len; i++) out[w + i] = (uint8_t)(v >> (8 * i));
    }
}

void mesh_mailbox_write(mesh_mailbox m, size_t off, const uint8_t *bytes, size_t n, size_t span) {
    if (n > span) abort();
    for (size_t w = 0; w < span; w += 4) {
        uint32_t v = 0;
        for (size_t i = 0; i < 4; i++)
            if (w + i < n) v |= (uint32_t)bytes[w + i] << (8 * i);
        atomic_store_explicit(a32(m, off + w), v, memory_order_relaxed);
    }
}

void mesh_mailbox_zero(mesh_mailbox m, size_t off, size_t len) { mesh_mailbox_write(m, off, NULL, 0, len); }

mesh_words mesh_mailbox_words(mesh_mailbox m) { return mesh_words_host(m.base); }

mesh_mailbox mesh_mailbox_test(void) {
    uint64_t *mem = calloc(MESH_MAILBOX_BYTES / 8, 8);
    if (!mem) abort();
    return mesh_mailbox_new((uint8_t *)mem, 0);
}
