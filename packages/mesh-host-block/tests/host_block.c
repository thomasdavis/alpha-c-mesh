/* Setters, counters, strings, and the snapshot copy with uptime and network counters. */
#include "mesh/abi/host_net.h"
#include "mesh/abi/host_status.h"
#include "mesh/chan.h"
#include "mesh/check.h"
#include "mesh/host_block.h"

#include <string.h>

int main(void) {
    static mesh_host_block h;
    mesh_host_block_init(&h);
    mesh_host_block_set_u32(&h, MESH_S_EPOCH, 7);
    mesh_host_block_add_u32(&h, MESH_S_HTTP_REQUESTS, 3);
    mesh_host_block_add_u32(&h, MESH_S_HTTP_REQUESTS, 4);
    mesh_host_block_set_f32(&h, MESH_S_TASKS_PER_S, 2.5f);
    mesh_host_block_set_u64(&h, MESH_S_LAST_BLINK_MS, 0x0102030405060708ull);
    mesh_host_block_set_str(&h, MESH_S_HOSTNAME, 32, "dgx-00");
    mesh_host_block_set_str(&h, MESH_S_LOCK_NAME, 4, "toolong");
    uint32_t a = 1, b = 2;
    mesh_host_field f[2] = {{MESH_S_SYSTEM, &a, 4}, {MESH_S_SYSTEMS, &b, 4}};
    mesh_host_block_set_many(&h, f, 2);
    CHECK_U(mesh_host_block_get_u32(&h, MESH_S_EPOCH), 7);
    CHECK_U(mesh_host_block_get_u32(&h, MESH_S_HTTP_REQUESTS), 7);
    CHECK(mesh_host_block_get_f32(&h, MESH_S_TASKS_PER_S) == 2.5f);
    CHECK_U(mesh_host_block_get_u32(&h, MESH_S_SYSTEMS), 2);
    mesh_net_counters_ctl(&h.net, 5, 9);
    mesh_sleep_us(20000);
    static uint8_t snap[MESH_SNAP_HOST];
    mesh_host_block_bytes(&h, snap);
    CHECK(memcmp(snap + MESH_S_HOSTNAME, "dgx-00\0", 7) == 0);
    CHECK(memcmp(snap + MESH_S_LOCK_NAME, "too\0", 4) == 0);
    CHECK(snap[MESH_S_LAST_BLINK_MS] == 8 && snap[MESH_S_LAST_BLINK_MS + 7] == 1);
    CHECK(snap[MESH_S_CTL_RX] == 5 && snap[MESH_S_CTL_TX] == 9);
    uint64_t up = 0;
    for (int i = 7; i >= 0; i--) up = up << 8 | snap[MESH_S_UPTIME_MS + (size_t)i];
    CHECK(up >= 15 && up < 5000);
    return mesh_check_done();
}
