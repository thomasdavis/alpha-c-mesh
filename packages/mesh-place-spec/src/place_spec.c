/* manifest.rs Place: region arithmetic. */
#include "mesh/place_spec.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <string.h>

static mps_region *copy(const mps_region *r, size_t n) {
    mps_region *c = mt_alloc((n ? n : 1) * sizeof *c);
    if (n) memcpy(c, r, n * sizeof *c);
    return c;
}

mps_spec mps_new(uint32_t cap, const mps_region *state, size_t nstate, uint32_t frame, const mps_region *out, size_t nout, uint32_t show, uint32_t fanout) {
    return (mps_spec){cap, copy(state, nstate), nstate, frame, copy(out, nout), nout, show, fanout};
}

mps_spec mps_clone(const mps_spec *s) { return mps_new(s->cap, s->state, s->nstate, s->frame, s->out, s->nout, s->show, s->fanout); }

void mps_free(mps_spec *s) {
    free(s->state);
    free(s->out);
    *s = (mps_spec){0};
}

size_t mps_state_bytes(const mps_spec *s) {
    size_t n = 0;
    for (size_t i = 0; i < s->nstate; i++) n += s->state[i].len;
    return n;
}

uint32_t mps_out_bytes(const mps_spec *s) {
    uint32_t n = 0;
    for (size_t i = 0; i < s->nout; i++) n += s->out[i].len;
    return n;
}

size_t mps_part_bytes(const mps_spec *s) { return (size_t)(mps_out_bytes(s) / s->fanout); }

void mps_part(const mps_spec *s, uint32_t b, mps_region *out) {
    for (size_t i = 0; i < s->nout; i++) {
        uint32_t n = s->out[i].len / s->fanout;
        out[i] = (mps_region){s->out[i].off + b * n, n};
    }
}

void mps_part_in_buffer(const mps_spec *s, uint32_t b, mps_region *out) {
    uint32_t at = 0;
    for (size_t i = 0; i < s->nout; i++) {
        uint32_t n = s->out[i].len / s->fanout;
        out[i] = (mps_region){at + b * n, n};
        at += s->out[i].len;
    }
}
