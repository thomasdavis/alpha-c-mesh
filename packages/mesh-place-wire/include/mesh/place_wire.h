/* @ajax/mesh-place-wire: the placement frames between machines (mesh-abi "placement", place.rs):
 *   F_PLACE: op, id, name, mode, aux, then bytes (a state, or a reason)
 *   F_FRAME: one frame's state (from the machine whose state it is; empty from a split follower) and
 *            the parts in `parts`, ascending
 *   F_INPUT: input bytes for a program's owner
 * Each frame is its type byte, a fixed head, then a body whose length the head gives; a hostile length
 * is refused before anything is read. */
#ifndef MESH_PLACE_WIRE_H
#define MESH_PLACE_WIRE_H

#include "mesh/abi/base.h"
#include "mesh/abi/placement.h"
#include "mesh/place_mode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* PL_REQ's aux for `split auto`: this bit and the member mask in the low bits. */
#define MPW_AUX_AUTO (1u << 31)
/* F_PLACE's fixed head, then len bytes. */
#define MPW_PLACE_HEAD (4 + 4 + MESH_NAME_BYTES + 4 + 4 + MESH_PLACE_PARTS_MAX + 4 + 4)
/* F_FRAME's fixed head, then state_len + data_len bytes. */
#define MPW_FRAME_HEAD (MESH_NAME_BYTES + 5 * 4)
/* F_INPUT's fixed head, then len bytes. */
#define MPW_INPUT_HEAD (MESH_NAME_BYTES + 4)

/* A name in its 16 wire bytes (NUL padded, at most NAME_MAX). */
void mpw_name16(const char *name, uint8_t out[MESH_NAME_BYTES]);
/* The name in wire bytes, checked ([a-z0-9-]{1,15}); false with *err. out holds NAME_BYTES + 1. */
bool mpw_name_of(const uint8_t *b, size_t n, char *out, char **err);

/* A growable byte buffer. */
typedef struct {
    uint8_t *b;
    size_t n, cap;
} mpw_bytes;

void mpw_put(mpw_bytes *o, const void *p, size_t n);
void mpw_put32(mpw_bytes *o, uint32_t v);
void mpw_bytes_free(mpw_bytes *o);
uint32_t mpw_u32_at(const uint8_t *b, size_t at);
uint64_t mpw_u64_at(const uint8_t *b, size_t at);

typedef struct {
    uint32_t op, id;
    char name[MESH_NAME_BYTES + 1];
    mpm_mode mode;
    uint32_t aux;
    uint8_t *bytes;
    size_t nbytes;
} mpw_place;

/* The frame, type byte first. */
mpw_bytes mpw_place_encode(const mpw_place *m);
/* The body's length beyond the head (checked against F_PLACE_MAX). */
bool mpw_place_rest_len(const uint8_t *head, size_t *n, char **err);
/* head: MPW_PLACE_HEAD bytes; rest: the body (copied). */
bool mpw_place_decode(const uint8_t *head, const uint8_t *rest, size_t nrest, mpw_place *out, char **err);
void mpw_place_free(mpw_place *m);

typedef struct {
    char name[MESH_NAME_BYTES + 1];
    uint32_t frame, parts, sys;
    uint8_t *state;
    size_t nstate;
    uint8_t *data;
    size_t ndata;
} mpw_frame;

mpw_bytes mpw_frame_encode(const mpw_frame *m);
bool mpw_frame_rest_len(const uint8_t *head, size_t *n, char **err);
bool mpw_frame_decode(const uint8_t *head, const uint8_t *rest, size_t nrest, mpw_frame *out, char **err);
mpw_frame mpw_frame_clone(const mpw_frame *m);
void mpw_frame_free(mpw_frame *m);

mpw_bytes mpw_input_encode(const char *name, const uint8_t *bytes, size_t n);
bool mpw_input_len(const uint8_t *head, size_t *n, char **err);

#endif
