/* manifest.rs's encode_entry and encode_ext: little-endian words and NUL-padded names. */
#include "mesh/manifest_encode.h"

#include "mesh/abi/base.h"
#include "mesh/abi/placement.h"

#include <string.h>

static void put(uint8_t *b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; i++) b[off + (size_t)i] = (uint8_t)(v >> (8 * i));
}

static void put_str(uint8_t *b, size_t off, const char *s, size_t max) {
    size_t n = strlen(s);
    if (n > max) n = max;
    memcpy(b + off, s, n);
}

void mesh_manifest_encode_entry(const mesh_manifest *m, const mesh_image *im, uint8_t b[MESH_PT_BYTES]) {
    memset(b, 0, MESH_PT_BYTES);
    put_str(b, MESH_PT_NAME, m->name, MESH_NAME_MAX);
    mesh_manifest_window w = m->has_request ? m->request : (mesh_manifest_window){0, 0, 0, 0};
    const struct {
        size_t off;
        uint32_t v;
    } words[] = {
        {MESH_PT_VIEW_OFF, im->view_off}, {MESH_PT_VIEW_BYTES, m->view_bytes}, {MESH_PT_SNAP_BYTES, m->snap_bytes},
        {MESH_PT_NODES, m->nodes}, {MESH_PT_WARPS, m->warps}, {MESH_PT_POLICY, m->policy}, {MESH_PT_HZ, m->hz},
        {MESH_PT_FANOUT, m->fanout}, {MESH_PT_PEER, m->peer}, {MESH_PT_READS_MASK, im->reads_mask},
        {MESH_PT_INPUT_OFF, m->input_off}, {MESH_PT_INPUT_LEN, m->input_len},
        {MESH_PT_REQ_OFF, w.req_off}, {MESH_PT_REQ_MAX, w.req_max}, {MESH_PT_RESP_OFF, w.resp_off}, {MESH_PT_RESP_MAX, w.resp_max},
        {MESH_PT_REGS, im->regs}, {MESH_PT_CODE_BYTES, im->code_bytes}, {MESH_PT_VIEW_ABI, m->view_abi},
        {MESH_PT_FLAGS, (m->batch ? MESH_FL_BATCH : 0u) | (m->autostart ? MESH_FL_AUTOSTART : 0u)},
        {MESH_PT_ARG_MODE, m->arg_mode}, {MESH_PT_ARG_OFF, m->arg_off}, {MESH_PT_RT_ABI, im->rt_abi}, {MESH_PT_LOADS, im->loads},
    };
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) put(b, words[i].off, words[i].v);
    for (int i = 0; i < 8; i++) b[MESH_PT_PTX_HASH + (size_t)i] = (uint8_t)(im->ptx_hash >> (8 * i));
}

void mesh_manifest_encode_ext(const mesh_manifest *m, uint8_t b[MESH_PX_BYTES]) {
    memset(b, 0, MESH_PX_BYTES);
    for (size_t i = 0; i < MESH_COUNTERS; i++) put_str(b, MESH_PX_COUNTER_NAMES + MESH_NAME_BYTES * i, m->counters[i], MESH_NAME_MAX);
    put(b, MESH_PX_FIELD_COUNT, (uint32_t)m->nfields);
    put(b, MESH_PX_PLACE_CAP, m->place ? m->place->cap : MESH_PLACE_CAP_NONE);
    put(b, MESH_PX_PARAM, m->param);
    put(b, MESH_PX_SHARED, m->shared_bytes);
    put(b, MESH_PX_SCRATCH_BYTES, m->scratch_bytes);
    for (size_t i = 0; i < m->nfields; i++) {
        size_t r = MESH_PX_FIELDS + MESH_FIELD_BYTES * i;
        put_str(b, r + MESH_FD_NAME, m->fields[i].name, MESH_FD_NAME_MAX);
        put(b, r + MESH_FD_TYPE, m->fields[i].ty);
        put(b, r + MESH_FD_OFF, m->fields[i].off);
    }
}
