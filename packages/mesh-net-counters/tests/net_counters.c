/* net.rs's counters_fill_the_block, one for one. */
#include "mesh/abi/host_net.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/snapshot.h"
#include "mesh/check.h"
#include "mesh/net_counters.h"

#include <stdlib.h>

static uint64_t u(const uint8_t *b, size_t o) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | b[o + (size_t)i];
    return v;
}

int main(void) {
    static mesh_net_counters c;
    mesh_net_counters_init(&c);
    uint8_t round[4] = {MESH_F_ROUND, 0, 0, 0}, beat[1] = {MESH_F_BEAT};
    mesh_net_counters_tx(&c, 2, round, 4);
    mesh_net_counters_tx(&c, 2, beat, 1);
    mesh_net_counters_rx(&c, 2, MESH_F_FRAME, 100);
    mesh_net_counters_rx(&c, 1, MESH_F_STATE, 7);
    mesh_net_counters_ctl(&c, 5, 1000);
    mesh_net_counters_http(&c, 30, 400);
    uint8_t *b = calloc(1, MESH_SNAP_HOST);
    mesh_net_counters_fill(&c, b);
    CHECK_U(u(b, mesh_link(2) + MESH_L_TX_BYTES), 5);
    CHECK_U(u(b, mesh_link(2) + MESH_L_RX_BYTES), 100);
    CHECK_U(u(b, mesh_link(1) + MESH_L_RX_BYTES), 7);
    CHECK_U(u(b, mesh_link(2) + MESH_L_TX_FRAMES + 8 * MESH_KIND_ROUND), 1);
    CHECK_U(u(b, mesh_link(2) + MESH_L_TX_FRAMES + 8 * MESH_KIND_CTL), 1);
    CHECK_U(u(b, mesh_link(2) + MESH_L_RX_FRAMES + 8 * MESH_KIND_PLACE), 1);
    CHECK_U(u(b, mesh_link(1) + MESH_L_RX_FRAMES + 8 * MESH_KIND_LOAD), 1);
    CHECK_U(u(b, MESH_S_CTL_RX), 5);
    CHECK_U(u(b, MESH_S_CTL_TX), 1000);
    CHECK_U(u(b, MESH_S_CTL_REQS), 1);
    CHECK_U(u(b, MESH_S_HTTP_RX), 30);
    CHECK_U(u(b, MESH_S_HTTP_TX), 400);
    CHECK_U(u(b, MESH_S_HTTP_CONNS), 1);
    free(b);
    return mesh_check_done();
}
