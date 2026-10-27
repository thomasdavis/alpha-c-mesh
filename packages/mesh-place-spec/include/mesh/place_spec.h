/* @ajax/mesh-place-spec: what a placeable program's manifest declares (manifest.rs Place): its
 * `place` capability, the shared state regions, the frame number's offset, the output regions
 * (part b of a frame is the b-th of `fanout` equal pieces of each) and the show region. The manifest
 * parser fills one of these; placement (mesh-placer) moves exactly these bytes. */
#ifndef MESH_PLACE_SPEC_H
#define MESH_PLACE_SPEC_H

#include <stddef.h>
#include <stdint.h>

/* (view offset, bytes) */
typedef struct {
    uint32_t off, len;
} mps_region;

typedef struct {
    uint32_t cap;          /* PLACE_CAP_PIN / PLACE_CAP_SPLIT */
    mps_region *state;     /* the shared state, each 4-aligned */
    size_t nstate;
    uint32_t frame;        /* the u32 frame number's view offset (inside a state region) */
    mps_region *out;       /* the output regions */
    size_t nout;
    uint32_t show;         /* the show region's view offset (SH_*) */
    uint32_t fanout;
} mps_spec;

mps_spec mps_new(uint32_t cap, const mps_region *state, size_t nstate, uint32_t frame, const mps_region *out, size_t nout, uint32_t show, uint32_t fanout);
mps_spec mps_clone(const mps_spec *s);
void mps_free(mps_spec *s);
size_t mps_state_bytes(const mps_spec *s);
uint32_t mps_out_bytes(const mps_spec *s);
size_t mps_part_bytes(const mps_spec *s);
/* Part b's (view offset, bytes) in each output region: out[0..nout). */
void mps_part(const mps_spec *s, uint32_t b, mps_region *out);
/* Part b's (offset, bytes) pieces within a show buffer (the output regions concatenated). */
void mps_part_in_buffer(const mps_spec *s, uint32_t b, mps_region *out);

#endif
