/* pipehash on the simulator between a host source and a host sink: every msg
 * pushed into its inbox comes out of the sink as its digest (sha256d on the
 * GPU path), exactly once; with no sink it does nothing; a full sink stalls the
 * warp before it pops (backpressure), and the msgs wait in its inbox. */
#include "mesh/abi/pipe.h"
#include "mesh/check.h"
#include "mesh/rt/sim_mailbox.h"
#include "mesh/words_host.h"

#include "../src/pipehash.c"

#include <stdlib.h>

#define SRC 2
#define HASH 0
#define SINK 1
#define N 200

static void le(const uint32_t *w, size_t n, uint8_t *out) {
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 4; k++) out[4 * i + k] = (uint8_t)(w[i] >> (8 * k));
}

int main(void) {
    uint8_t *mb = mesh_sim_mailbox();
    mesh_words w = mesh_words_host(mb);
    mesh_sim_prog hp = {.name = "pipehash", .view_off = MESH_VIEW_HEAP, .view_bytes = 0x16000, .nodes = 2};
    mesh_sim_install(mb, HASH, &hp);
    mesh_sim_inbox(mb, HASH, MESH_PIPEDEMO_H_INBOX, MESH_PIPEDEMO_H_CAP, MESH_PIPEDEMO_MSG_BYTES, MESH_PIPEDEMO_MSG_TYPE);
    mesh_sim_prog src = {.name = "pipesrc", .view_off = MESH_VIEW_HEAP + 0x20000, .view_bytes = 0x1000};
    mesh_sim_install(mb, SRC, &src);
    mesh_sim_feed(mb, SRC, HASH);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, HASH, &hp, mesh_main);
    /* No sink yet: a task finds none and does nothing. */
    CHECK_U(mesh_sim_call(mb, HASH, MESH_OP_TASK, 0), 0);
    CHECK_U(mesh_sim_u32(mesh_sim_view(mb, HASH) + MESH_PIPEDEMO_H_DST), 0);
    /* The sink: 64 cells (two sides of 32): room for one warp's digests at a time. */
    mesh_sim_prog sk = {.name = "pipesink", .view_off = MESH_VIEW_HEAP + 0x30000, .view_bytes = 0x8000};
    mesh_sim_install(mb, SINK, &sk);
    mesh_sim_inbox(mb, SINK, 0x80, 64, MESH_PIPEDEMO_DIGEST_BYTES, MESH_PIPEDEMO_DIGEST_TYPE);
    mesh_sim_feed(mb, HASH, SINK);
    /* The source's msgs. */
    static uint32_t msgs[N][16];
    for (uint32_t s = 0; s < N; s++) {
        mesh_pipedemo_msg(s, 1000 + s, SRC, msgs[s]);
        uint8_t b[64];
        le(msgs[s], 16, b);
        CHECK_I(mesh_pipe_push(&w, SRC, HASH, b, 64), 1);
    }
    /* Run tasks and drain the sink until every digest arrived. */
    static bool seen[N];
    uint32_t got = 0, rounds = 0;
    while (got < N && rounds++ < 2000) {
        uint32_t t = mesh_sim_publish(mb, HASH, MESH_OP_TASK, 0), r;
        CHECK(mesh_sim_wait(mb, HASH, t, &r, 60000));
        uint8_t out[48];
        while (mesh_pipe_pop(&w, SINK, out, 48) == 1) {
            uint32_t dg[12];
            for (int k = 0; k < 12; k++) dg[k] = (uint32_t)out[4 * k] | (uint32_t)out[4 * k + 1] << 8 | (uint32_t)out[4 * k + 2] << 16 | (uint32_t)out[4 * k + 3] << 24;
            bool found = false;
            for (uint32_t s = 0; s < N && !found; s++) {
                uint32_t want[12];
                mesh_pipedemo_digest(msgs[s], MESH_PIPEDEMO_PATH_GPU, want);
                if (memcmp(want, dg, sizeof want) == 0) {
                    CHECK(!seen[s]);
                    seen[s] = true;
                    found = true;
                    got++;
                }
            }
            CHECK(found);
        }
    }
    CHECK_U(got, N);
    uint8_t *ctl = mesh_sim_ctl(mb, HASH);
    CHECK_U(mesh_sim_u32(ctl + MESH_P_COUNTERS), N);
    CHECK_U(mesh_sim_u32(mesh_sim_view(mb, HASH) + MESH_PIPEDEMO_H_DST), SINK + 1);
    /* Backpressure: 32 msgs hashed and not drained fill the sink's GPU side
     * (32 cells); then the warp pops nothing and the next msgs wait. */
    for (uint32_t s = 0; s < 32; s++) {
        uint8_t b[64];
        le(msgs[s], 16, b);
        CHECK_I(mesh_pipe_push(&w, SRC, HASH, b, 64), 1);
    }
    CHECK_U(mesh_sim_call(mb, HASH, MESH_OP_TASK, 0), 32);
    for (uint32_t s = 0; s < 5; s++) {
        uint8_t b[64];
        le(msgs[s], 16, b);
        CHECK_I(mesh_pipe_push(&w, SRC, HASH, b, 64), 1);
    }
    uint32_t stalls = mesh_sim_u32(ctl + MESH_P_COUNTERS + 4);
    CHECK_U(mesh_sim_call(mb, HASH, MESH_OP_TASK, 0), 0);
    CHECK(mesh_sim_u32(ctl + MESH_P_COUNTERS + 4) > stalls);
    mesh_pipe_inbox in;
    CHECK_I(mesh_pipe_target(&w, HASH, HASH, &in), 0);
    CHECK_U(mesh_pipe_inbox_len(&in, &w), 5);
    CHECK_U(mesh_sim_u32(ctl + MESH_P_FAULT), 0);
    mesh_sim_halt(mb, HASH, l);
    mesh_sim_mailbox_free(mb);
    return mesh_check_done();
}
