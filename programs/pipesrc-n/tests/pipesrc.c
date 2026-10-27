/* pipesrc-n on exec's node loop: msgs into pipehash's inbox in order (mode
 * 0), digests straight into a sink (mode 1), backpressure when full, `gone`
 * without a consumer, and its ops. */
#include "harness.h"
#include "../src/pipesrc.h"

#include "mesh/abi/pipe.h"
#include "mesh/abi/view_pipedemo.h"

#define SRC 2
#define HASH 0
#define SINK 1

int main(void) {
    mesh_mailbox mb = mesh_mailbox_test();
    mesh_words w = mesh_mailbox_words(mb);
    h_install(mb, SRC, "pipesrc-n", 1, 0x100000, 0x100);
    h_install(mb, HASH, "pipehash", 1, 0x110000, 0x16000);
    h_inbox(mb, HASH, MESH_PIPEDEMO_H_INBOX, 128, 64, MESH_PIPEDEMO_MSG_TYPE);
    h_feed(mb, SRC, HASH);
    h_slot h = {.mb = mb, .slot = SRC, .prog = {pipesrc_init, pipesrc_task, NULL}};
    h_start(&h);
    CHECK_U(h_call(&h, MESH_OP_INIT, 0), MESH_R_NONE);
    size_t v = 0x100000;
    CHECK_U(mesh_mailbox_get(mb, v + MESH_PIPEDEMO_S_BATCH), MESH_PIPEDEMO_SRC_BATCH);
    /* Mode 0: one batch of 64 msgs, in seq order on the CPU side. */
    CHECK_U(h_call(&h, MESH_OP_TASK, 0), 64);
    CHECK_U(mesh_mailbox_get(mb, v + MESH_PIPEDEMO_S_DST), HASH + 1);
    uint8_t rec[64];
    for (uint32_t s = 0; s < 64; s++) {
        CHECK_I(mesh_pipe_pop(&w, HASH, rec, 64), 1);
        uint32_t seq = (uint32_t)rec[0] | (uint32_t)rec[1] << 8 | (uint32_t)rec[2] << 16 | (uint32_t)rec[3] << 24;
        CHECK_U(seq, s);
        CHECK_U(rec[12], MESH_PIPEDEMO_ORIGIN_NATIVE);
    }
    /* Backpressure: the CPU side holds 64; the second batch of 64 fills it, the third is refused. */
    CHECK_U(h_call(&h, MESH_OP_TASK, 0), 64);
    CHECK_U(h_call(&h, MESH_OP_TASK, 0), 0);
    CHECK_U(mesh_mailbox_get(mb, v + MESH_PIPEDEMO_S_FULL), 1);
    CHECK_U(mesh_mailbox_get(mb, v + MESH_PIPEDEMO_S_SENT), 128);
    /* Mode 1 with no sink: gone. Then a sink: digests of msgs 128.. */
    CHECK_U(h_call(&h, MESH_PIPEDEMO_OP_MODE, MESH_PIPEDEMO_MODE_DIRECT), 0);
    CHECK_U(h_call(&h, MESH_OP_TASK, 0), 0);
    CHECK_U(mesh_mailbox_get(mb, v + MESH_PIPEDEMO_S_GONE), 1);
    h_install(mb, SINK, "pipesink", 1, 0x130000, 0x3d000);
    h_inbox(mb, SINK, MESH_PIPEDEMO_K_INBOX, 256, 48, MESH_PIPEDEMO_DIGEST_TYPE);
    h_feed(mb, SRC, SINK);
    CHECK_U(h_call(&h, MESH_PIPEDEMO_OP_BATCH, 5), 0);
    CHECK_U(h_call(&h, MESH_OP_TASK, 0), 5);
    uint8_t d[48];
    CHECK_I(mesh_pipe_pop(&w, SINK, d, 48), 1);
    uint32_t seq = (uint32_t)d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24;
    CHECK_U(seq, 128);
    /* Ops. */
    CHECK_U(h_call(&h, MESH_PIPEDEMO_OP_MODE, 9), MESH_R_BAD_OP);
    CHECK_U(h_call(&h, MESH_PIPEDEMO_OP_BATCH, 0), MESH_R_BAD_OP);
    CHECK_U(h_call(&h, MESH_PIPEDEMO_OP_INTERVAL, 500), 0);
    CHECK_U(h_call(&h, MESH_PIPEDEMO_OP_RESET, 0), 0);
    CHECK_U(mesh_mailbox_get(mb, v + MESH_PIPEDEMO_S_SENT), 0);
    CHECK_U(h_call(&h, MESH_PIPEDEMO_OP_MODE, MESH_PIPEDEMO_MODE_OFF), 0);
    CHECK_U(h_call(&h, MESH_OP_TASK, 0), 0);
    h_stop(&h);
    free(mb.base);
    return mesh_check_done();
}
