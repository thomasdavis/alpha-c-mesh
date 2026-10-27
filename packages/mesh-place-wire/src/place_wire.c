/* place.rs: name16, name_of, PlaceMsg, FrameMsg, encode_input, input_len. */
#include "mesh/place_wire.h"
#include "mesh/abi/name.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/abi/place_ops.h"
#include "mesh/rust_fmt.h"
#include "mesh/mt.h"

#include <stdlib.h>
#include <string.h>

void mpw_name16(const char *name, uint8_t out[MESH_NAME_BYTES]) {
    memset(out, 0, MESH_NAME_BYTES);
    size_t k = strlen(name);
    if (k > MESH_NAME_MAX) k = MESH_NAME_MAX;
    memcpy(out, name, k);
}

bool mpw_name_of(const uint8_t *b, size_t n, char *out, char **err) {
    size_t k = 0;
    while (k < n && b[k]) k++;
    if (!mesh_valid_name(b, k)) {
        char *raw = mt_strndup((const char *)b, k);
        char *q = rf_debug_str(raw);
        *err = mt_fmt("bad program name %s", q);
        free(q), free(raw);
        return false;
    }
    memcpy(out, b, k);
    out[k] = 0;
    return true;
}

void mpw_put(mpw_bytes *o, const void *p, size_t n) {
    if (o->n + n > o->cap) {
        size_t c = o->cap ? o->cap : 64;
        while (c < o->n + n) c *= 2;
        o->b = mt_realloc(o->b, c);
        o->cap = c;
    }
    if (n) memcpy(o->b + o->n, p, n);
    o->n += n;
}

void mpw_put32(mpw_bytes *o, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    mpw_put(o, b, 4);
}

void mpw_bytes_free(mpw_bytes *o) {
    free(o->b);
    *o = (mpw_bytes){0};
}

uint32_t mpw_u32_at(const uint8_t *b, size_t at) { return (uint32_t)b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24; }
uint64_t mpw_u64_at(const uint8_t *b, size_t at) { return (uint64_t)mpw_u32_at(b, at) | (uint64_t)mpw_u32_at(b, at + 4) << 32; }

static uint8_t *dup(const uint8_t *p, size_t n) {
    uint8_t *d = mt_alloc(n ? n : 1);
    if (n) memcpy(d, p, n);
    return d;
}

mpw_bytes mpw_place_encode(const mpw_place *m) {
    mpw_bytes o = {0};
    uint8_t t = MESH_F_PLACE, name[MESH_NAME_BYTES], assign[MESH_PLACE_PARTS_MAX];
    uint32_t code, sys;
    mpm_code(&m->mode, &code, &sys);
    mpw_put(&o, &t, 1);
    mpw_put32(&o, m->op);
    mpw_put32(&o, m->id);
    mpw_name16(m->name, name);
    mpw_put(&o, name, sizeof name);
    mpw_put32(&o, code);
    mpw_put32(&o, sys);
    mpm_assign_bytes(&m->mode, assign);
    mpw_put(&o, assign, sizeof assign);
    mpw_put32(&o, m->aux);
    mpw_put32(&o, (uint32_t)m->nbytes);
    mpw_put(&o, m->bytes, m->nbytes);
    return o;
}

bool mpw_place_rest_len(const uint8_t *head, size_t *n, char **err) {
    *n = mpw_u32_at(head, MPW_PLACE_HEAD - 4);
    if (*n > MESH_F_PLACE_MAX) {
        *err = mt_fmt("F_PLACE of %zu bytes > F_PLACE_MAX", *n);
        return false;
    }
    return true;
}

bool mpw_place_decode(const uint8_t *head, const uint8_t *rest, size_t nrest, mpw_place *out, char **err) {
    uint32_t op = mpw_u32_at(head, 0);
    if (op > MESH_PL_FAIL) {
        *err = mt_fmt("F_PLACE: bad op %u", op);
        return false;
    }
    size_t at = 8 + MESH_NAME_BYTES;
    mpw_place m = {.op = op, .id = mpw_u32_at(head, 4)};
    if (!mpw_name_of(head + 8, MESH_NAME_BYTES, m.name, err)) return false;
    if (!mpm_from_code(mpw_u32_at(head, at), mpw_u32_at(head, at + 4), head + at + 8, MESH_PLACE_PARTS_MAX, &m.mode, err)) return false;
    m.aux = mpw_u32_at(head, at + 8 + MESH_PLACE_PARTS_MAX);
    m.bytes = dup(rest, nrest);
    m.nbytes = nrest;
    *out = m;
    return true;
}

void mpw_place_free(mpw_place *m) {
    free(m->bytes);
    m->bytes = NULL;
    m->nbytes = 0;
}

mpw_bytes mpw_frame_encode(const mpw_frame *m) {
    mpw_bytes o = {0};
    uint8_t t = MESH_F_FRAME, name[MESH_NAME_BYTES];
    mpw_put(&o, &t, 1);
    mpw_name16(m->name, name);
    mpw_put(&o, name, sizeof name);
    mpw_put32(&o, m->frame);
    mpw_put32(&o, m->parts);
    mpw_put32(&o, m->sys);
    mpw_put32(&o, (uint32_t)m->nstate);
    mpw_put32(&o, (uint32_t)m->ndata);
    mpw_put(&o, m->state, m->nstate);
    mpw_put(&o, m->data, m->ndata);
    return o;
}

bool mpw_frame_rest_len(const uint8_t *head, size_t *n, char **err) {
    *n = (size_t)mpw_u32_at(head, MESH_NAME_BYTES + 12) + mpw_u32_at(head, MESH_NAME_BYTES + 16);
    if (*n > MESH_F_PLACE_MAX) {
        *err = mt_fmt("F_FRAME of %zu bytes > F_PLACE_MAX", *n);
        return false;
    }
    return true;
}

bool mpw_frame_decode(const uint8_t *head, const uint8_t *rest, size_t nrest, mpw_frame *out, char **err) {
    size_t sl = mpw_u32_at(head, MESH_NAME_BYTES + 12);
    if (sl > nrest) sl = nrest;
    mpw_frame m = {.frame = mpw_u32_at(head, MESH_NAME_BYTES), .parts = mpw_u32_at(head, MESH_NAME_BYTES + 4), .sys = mpw_u32_at(head, MESH_NAME_BYTES + 8)};
    if (!mpw_name_of(head, MESH_NAME_BYTES, m.name, err)) return false;
    m.state = dup(rest, sl);
    m.nstate = sl;
    m.data = dup(rest + sl, nrest - sl);
    m.ndata = nrest - sl;
    *out = m;
    return true;
}

mpw_frame mpw_frame_clone(const mpw_frame *m) {
    mpw_frame c = *m;
    c.state = dup(m->state, m->nstate);
    c.data = dup(m->data, m->ndata);
    return c;
}

void mpw_frame_free(mpw_frame *m) {
    free(m->state);
    free(m->data);
    m->state = m->data = NULL;
    m->nstate = m->ndata = 0;
}

mpw_bytes mpw_input_encode(const char *name, const uint8_t *bytes, size_t n) {
    mpw_bytes o = {0};
    uint8_t t = MESH_F_INPUT, nm[MESH_NAME_BYTES];
    mpw_put(&o, &t, 1);
    mpw_name16(name, nm);
    mpw_put(&o, nm, sizeof nm);
    mpw_put32(&o, (uint32_t)n);
    mpw_put(&o, bytes, n);
    return o;
}

bool mpw_input_len(const uint8_t *head, size_t *n, char **err) {
    *n = mpw_u32_at(head, MESH_NAME_BYTES);
    if (*n > MESH_F_INPUT_MAX) {
        *err = mt_fmt("F_INPUT of %zu bytes > F_INPUT_MAX", *n);
        return false;
    }
    return true;
}
