/* @ajax/mesh-mailbox: host views of the mailbox words (mesh-host mailbox.rs).
 * The GPU (and workers, and lanes) write the same memory concurrently, so
 * every access is an atomic of the word's size: release stores publish,
 * acquire loads observe completion.  An offset out of range or misaligned is
 * a bug: it aborts with the offset, as the Rust's assert panics. */
#ifndef MESH_MAILBOX_H
#define MESH_MAILBOX_H

#include "mesh/abi/words.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The mailbox: host address and the device address the kernel receives. */
typedef struct {
    uint8_t *base;
    uint64_t dev;
} mesh_mailbox;

/* base must point to MAILBOX_BYTES of 8-aligned memory that outlives every use. */
static inline mesh_mailbox mesh_mailbox_new(uint8_t *base, uint64_t dev) {
    mesh_mailbox m = {base, dev};
    return m;
}

uint32_t mesh_mailbox_get(mesh_mailbox m, size_t off);                 /* acquire load */
void mesh_mailbox_set(mesh_mailbox m, size_t off, uint32_t v);         /* payload store (relaxed) */
void mesh_mailbox_publish(mesh_mailbox m, size_t off, uint32_t v);     /* release store */
uint32_t mesh_mailbox_fetch_add(mesh_mailbox m, size_t off, uint32_t v); /* AcqRel, returns the old value */
/* Compare-and-swap (AcqRel); returns the value seen (== cur on success). */
uint32_t mesh_mailbox_cas(mesh_mailbox m, size_t off, uint32_t cur, uint32_t desired);
uint64_t mesh_mailbox_fetch_add64(mesh_mailbox m, size_t off, uint64_t v);
uint8_t mesh_mailbox_get8(mesh_mailbox m, size_t off);
void mesh_mailbox_set8(mesh_mailbox m, size_t off, uint8_t v);
uint64_t mesh_mailbox_get64(mesh_mailbox m, size_t off);
void mesh_mailbox_set64(mesh_mailbox m, size_t off, uint64_t v);
void mesh_mailbox_publish64(mesh_mailbox m, size_t off, uint64_t v);
/* len bytes at off (word reads; off 4-aligned) into out. */
void mesh_mailbox_read(mesh_mailbox m, size_t off, uint8_t *out, size_t len);
/* Writes bytes at off, zero padding to span bytes (word stores). */
void mesh_mailbox_write(mesh_mailbox m, size_t off, const uint8_t *bytes, size_t n, size_t span);
void mesh_mailbox_zero(mesh_mailbox m, size_t off, size_t len);
/* The pipe words over it (mesh-abi pipe): absolute mailbox offsets. */
mesh_words mesh_mailbox_words(mesh_mailbox m);

/* A heap mailbox for tests (no GPU): MAILBOX_BYTES zeroed, 8-aligned. Free its base. */
mesh_mailbox mesh_mailbox_test(void);

#endif
