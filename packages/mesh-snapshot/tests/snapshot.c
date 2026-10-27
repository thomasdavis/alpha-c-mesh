/* status.rs's layout test. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "mesh/abi.h"
#include "mesh/check.h"
#include "mesh/manifest.h"
#include "mesh/manifest_encode.h"
#include "mesh/prog_table.h"
#include "mesh/snapshot.h"

#include <stdlib.h>
#include <string.h>

static mesh_buf s;
static uint32_t u(size_t o) { return (uint32_t)(uint8_t)s.data[o] | (uint32_t)(uint8_t)s.data[o + 1] << 8 | (uint32_t)(uint8_t)s.data[o + 2] << 16 | (uint32_t)(uint8_t)s.data[o + 3] << 24; }

int main(void) {
    mesh_mailbox mb = mesh_mailbox_test();
    mesh_mailbox_set(mb, MESH_H_MAGIC, MESH_MAGIC);
    mesh_mailbox_set(mb, MESH_H_EPOCH, 7);
    mesh_manifest m;
    char *err = NULL;
    const char *text = "name = \"hello\"\nabi = 6\nview_bytes = 0x1000\nsnap_bytes = 6\nnodes = 2";
    CHECK(mesh_manifest_parse(text, strlen(text), &m, &err));
    mesh_image im;
    memset(&im, 0, sizeof im);
    im.view_off = 0x100000;
    im.loads = 1;
    uint8_t entry[MESH_PT_BYTES], ext[MESH_PX_BYTES];
    mesh_manifest_encode_entry(&m, &im, entry);
    mesh_manifest_encode_ext(&m, ext);
    mesh_pt_write_entry(mb, 4, entry, ext, MESH_ST_RUNNING);
    mesh_mailbox_set(mb, 0x100000, 0x4f4c4548u);
    mesh_mailbox_set(mb, 0x100004, 0xffffffffu);
    mesh_mailbox_set(mb, mesh_prog_ctl(4) + MESH_P_DONE, 99);
    mesh_mailbox_set(mb, mesh_node_rec(4, 1) + MESH_N_HEARTBEAT, 5);
    static mesh_host_block h;
    mesh_host_block_init(&h);
    mesh_host_block_set_u32(&h, MESH_S_EPOCH, 7);
    mesh_snapshot(&s, mb, &h);
    CHECK(u(MESH_SN_TOTAL) == s.len && u(MESH_SN_COUNT) == 1 && u(MESH_SN_VERSION) == MESH_SNAP_VERSION);
    CHECK(u(MESH_SN_HEAD_BYTES + MESH_H_MAGIC) == MESH_MAGIC && u(MESH_SN_HEAD_BYTES + MESH_H_EPOCH) == 7);
    size_t sec = MESH_SN_HEAD_BYTES + MESH_H_BYTES;
    CHECK(u(sec + MESH_SC_SLOT) == 4 && u(sec + MESH_SC_BYTES) == mesh_section_bytes(2, 6) && u(sec + MESH_SC_NODES) == 2 &&
          u(sec + MESH_SC_VIEW) == 6);
    CHECK(memcmp(s.data + sec + MESH_SC_ENTRY + MESH_PT_NAME, "hello\0", 6) == 0);
    CHECK_U(u(sec + MESH_SC_ENTRY + MESH_PT_STATE), MESH_ST_RUNNING);
    CHECK_U(u(sec + MESH_SC_CTL + MESH_P_DONE), 99);
    CHECK_U(u(sec + MESH_SC_NODE_RECS + MESH_NODE_STRIDE + MESH_N_HEARTBEAT), 5);
    CHECK_U(u(sec + MESH_SC_NODE_RECS + 2 * MESH_NODE_STRIDE), 0x4f4c4548u);
    CHECK_U(u(sec + MESH_SC_NODE_RECS + 2 * MESH_NODE_STRIDE + 4), 0xffff);
    size_t host_at = u(MESH_SN_HOST_AT);
    CHECK(host_at == sec + mesh_section_bytes(2, 6) && s.len - host_at == MESH_SNAP_HOST);
    CHECK_U(u(host_at + MESH_S_EPOCH), 7);
    mesh_manifest_free(&m);
    mesh_buf_free(&s);
    free(mb.base);
    return mesh_check_done();
}
