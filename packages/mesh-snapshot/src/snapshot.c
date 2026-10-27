/* Snapshot v3, section by section. */
#include "mesh/snapshot.h"
#include "mesh/abi.h"
#include "mesh/prog_table.h"

#include <stdlib.h>
#include <string.h>

static uint32_t w32(const uint8_t *b, size_t o) { return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }

static void mb_append(mesh_buf *out, const mesh_mailbox *mb, size_t off, size_t len) {
    size_t at = out->len;
    mesh_buf_zeros(out, len);
    mesh_mailbox_read(mb, off, out->data + at, len);
}

void mesh_snapshot(mesh_buf *s, const mesh_mailbox *mb, mesh_host_block *host) {
    size_t start = s->len;
    mesh_buf_zeros(s, MESH_SN_HEAD_BYTES);
    mb_append(s, mb, 0, MESH_H_BYTES);
    uint32_t count = 0;
    for (uint32_t slot = 0; slot < MESH_PROG_SLOTS; slot++) {
        if (mesh_mailbox_get(mb, mesh_prog_entry(slot) + MESH_PT_STATE) == MESH_ST_EMPTY) continue;
        uint8_t entry[MESH_PT_BYTES];
        mesh_pt_read_entry(mb, slot, entry);
        uint32_t nodes = w32(entry, MESH_PT_NODES), view_off = w32(entry, MESH_PT_VIEW_OFF);
        uint32_t snap = w32(entry, MESH_PT_SNAP_BYTES), vb = w32(entry, MESH_PT_VIEW_BYTES);
        if (nodes > MESH_PROG_MAX_NODES) nodes = MESH_PROG_MAX_NODES;
        if (snap > vb) snap = vb;
        mesh_buf_u32le(s, slot);
        mesh_buf_u32le(s, (uint32_t)mesh_section_bytes(nodes, snap));
        mesh_buf_u32le(s, nodes);
        mesh_buf_u32le(s, snap);
        mesh_buf_put(s, entry, MESH_PT_BYTES);
        mb_append(s, mb, mesh_prog_ctl(slot), MESH_SC_CTL_BYTES);
        mb_append(s, mb, mesh_prog_ext(slot), MESH_PX_BYTES);
        mb_append(s, mb, mesh_node_rec(slot, 0), (size_t)MESH_NODE_STRIDE * nodes);
        size_t padded = ((size_t)snap + 3) / 4 * 4;
        if (snap > 0) {
            size_t at = s->len;
            mb_append(s, mb, view_off, padded);
            memset(s->data + at + snap, 0, padded - snap); /* zero padded, not the view's next bytes */
        }
        count++;
    }
    uint32_t host_at = (uint32_t)(s->len - start);
    size_t at = s->len;
    mesh_buf_zeros(s, MESH_SNAP_HOST);
    mesh_host_block_bytes(host, (uint8_t *)s->data + at);
    uint32_t total = (uint32_t)(s->len - start);
    const uint32_t v[4][2] = {{MESH_SN_TOTAL, total}, {MESH_SN_COUNT, count}, {MESH_SN_VERSION, MESH_SNAP_VERSION}, {MESH_SN_HOST_AT, host_at}};
    for (int i = 0; i < 4; i++)
        for (int b = 0; b < 4; b++) s->data[start + v[i][0] + (size_t)b] = (char)(v[i][1] >> (8 * b));
}
