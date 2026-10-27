/* Relaxed atomic counters, copied little-endian into the host block. */
#include "mesh/net_counters.h"
#include "mesh/abi/host_net.h"

#include <string.h>

void mesh_net_counters_init(mesh_net_counters *c) {
    for (size_t k = 0; k < MESH_MAX_SYSTEMS; k++) {
        mesh_link_counters *l = &c->links[k];
        atomic_init(&l->rx_bytes, 0);
        atomic_init(&l->tx_bytes, 0);
        for (size_t i = 0; i < MESH_NET_KINDS; i++) { atomic_init(&l->rx[i], 0); atomic_init(&l->tx[i], 0); }
    }
    for (size_t i = 0; i < 3; i++) { atomic_init(&c->ctl[i], 0); atomic_init(&c->http[i], 0); }
}

static void add(_Atomic uint64_t *a, uint64_t n) { atomic_fetch_add_explicit(a, n, memory_order_relaxed); }

void mesh_net_counters_rx(mesh_net_counters *c, uint32_t k, uint8_t ty, size_t bytes) {
    mesh_link_counters *l = &c->links[k];
    add(&l->rx_bytes, bytes);
    add(&l->rx[mesh_frame_kind(ty)], 1);
}

void mesh_net_counters_tx(mesh_net_counters *c, uint32_t k, const uint8_t *f, size_t len) {
    mesh_link_counters *l = &c->links[k];
    add(&l->tx_bytes, len);
    add(&l->tx[mesh_frame_kind(f[0])], 1);
}

void mesh_net_counters_ctl(mesh_net_counters *c, size_t rx, size_t tx) {
    add(&c->ctl[0], rx);
    add(&c->ctl[1], tx);
    add(&c->ctl[2], 1);
}

void mesh_net_counters_http(mesh_net_counters *c, size_t rx, size_t tx) {
    add(&c->http[0], rx);
    add(&c->http[1], tx);
    add(&c->http[2], 1);
}

static void put(uint8_t *b, size_t off, _Atomic uint64_t *a) {
    uint64_t v = atomic_load_explicit(a, memory_order_relaxed);
    for (int i = 0; i < 8; i++) b[off + (size_t)i] = (uint8_t)(v >> (8 * i));
}

void mesh_net_counters_fill(mesh_net_counters *c, uint8_t *b) {
    for (uint32_t k = 0; k < MESH_MAX_SYSTEMS; k++) {
        mesh_link_counters *l = &c->links[k];
        size_t at = mesh_link(k);
        put(b, at + MESH_L_RX_BYTES, &l->rx_bytes);
        put(b, at + MESH_L_TX_BYTES, &l->tx_bytes);
        for (size_t i = 0; i < MESH_NET_KINDS; i++) {
            put(b, at + MESH_L_RX_FRAMES + 8 * i, &l->rx[i]);
            put(b, at + MESH_L_TX_FRAMES + 8 * i, &l->tx[i]);
        }
    }
    const size_t ctl[3] = {MESH_S_CTL_RX, MESH_S_CTL_TX, MESH_S_CTL_REQS};
    const size_t http[3] = {MESH_S_HTTP_RX, MESH_S_HTTP_TX, MESH_S_HTTP_CONNS};
    for (size_t i = 0; i < 3; i++) { put(b, ctl[i], &c->ctl[i]); put(b, http[i], &c->http[i]); }
}
