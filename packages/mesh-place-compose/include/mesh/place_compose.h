/* @ajax/mesh-place-compose: moving a placed program's bytes (place.rs): its state regions read and
 * written through the view, its frame parts gathered and scattered, and the Composer, which matches the
 * parts of a frame arriving from several machines and yields a frame only when it has every part, all
 * of the same frame. */
#ifndef MESH_PLACE_COMPOSE_H
#define MESH_PLACE_COMPOSE_H

#include "mesh/place_spec.h"
#include "mesh/place_wire.h"

#include <stdbool.h>
#include <stdint.h>

/* Reads (view offset, bytes) into out (len bytes). */
typedef void (*mpc_read_fn)(void *ctx, uint32_t off, uint32_t len, uint8_t *out);
/* Writes bytes at a view offset. */
typedef void (*mpc_write_fn)(void *ctx, uint32_t off, const uint8_t *b, uint32_t len);

/* The state regions, concatenated. */
mpw_bytes mpc_read_state(const mps_spec *s, mpc_read_fn read, void *ctx);
/* Scatters concatenated state back to its regions; false (with *err) on a wrong length. */
bool mpc_write_state(const mps_spec *s, const uint8_t *b, size_t n, mpc_write_fn write, void *ctx, char **err);
/* The parts in mask (ascending), each part's piece of every output region. */
mpw_bytes mpc_gather(const mps_spec *s, uint32_t mask, mpc_read_fn read, void *ctx);
/* Places gathered parts into a show buffer (the output regions concatenated). */
bool mpc_scatter(const mps_spec *s, uint8_t *buf, size_t nbuf, uint32_t mask, const uint8_t *data, size_t ndata, char **err);
/* SHB_SYS for parts mask all rendered by sys (4 bits per part b < 8). */
uint32_t mpc_sys_nibbles(uint32_t mask, uint32_t sys);

/* Frames whose other parts have not arrived are kept this many deep. */
#define MPC_PENDING 8

typedef struct {
    uint32_t frame, have, sys;
    uint8_t *buf;
    uint64_t t0;
} mpc_pending;

typedef struct {
    mpc_pending q[MPC_PENDING];
    size_t n;
} mpc_composer;

/* A frame with every part: the frame, SHB_SYS (who rendered each part), the buffer (taken), and when
 * its first part came. */
typedef struct {
    uint32_t frame, sys;
    uint8_t *buf;
    size_t nbuf;
    uint64_t t0;
} mpc_whole;

/* Adds parts of a frame: 1 with *whole when the frame is complete, 0 when not yet, -1 with *err. */
int mpc_add(mpc_composer *c, const mps_spec *s, uint32_t frame, uint32_t parts, uint32_t sys, const uint8_t *data, size_t n, uint64_t now, mpc_whole *whole,
            char **err);
void mpc_clear(mpc_composer *c);

#endif
