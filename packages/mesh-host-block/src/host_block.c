/* The host status block under a mutex. */
#include "mesh/host_block.h"
#include "mesh/abi/host_status.h"
#include "mesh/chan.h"

#include <string.h>

void mesh_host_block_init(mesh_host_block *h) {
    memset(h->block, 0, sizeof h->block);
    pthread_mutex_init(&h->mu, NULL);
    h->started_us = mesh_mono_us();
    mesh_net_counters_init(&h->net);
}

static void put(mesh_host_block *h, size_t off, const void *b, size_t n) {
    pthread_mutex_lock(&h->mu);
    memcpy(h->block + off, b, n);
    pthread_mutex_unlock(&h->mu);
}

static void le32(uint8_t b[4], uint32_t v) { for (int i = 0; i < 4; i++) b[i] = (uint8_t)(v >> (8 * i)); }

void mesh_host_block_set_u32(mesh_host_block *h, size_t off, uint32_t v) {
    uint8_t b[4];
    le32(b, v);
    put(h, off, b, 4);
}

void mesh_host_block_set_f32(mesh_host_block *h, size_t off, float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    mesh_host_block_set_u32(h, off, u);
}

void mesh_host_block_set_u64(mesh_host_block *h, size_t off, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
    put(h, off, b, 8);
}

void mesh_host_block_set_many(mesh_host_block *h, const mesh_host_field *f, size_t n) {
    pthread_mutex_lock(&h->mu);
    for (size_t i = 0; i < n; i++) memcpy(h->block + f[i].off, f[i].bytes, f[i].len);
    pthread_mutex_unlock(&h->mu);
}

static uint32_t get32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

void mesh_host_block_add_u32(mesh_host_block *h, size_t off, uint32_t n) {
    pthread_mutex_lock(&h->mu);
    le32(h->block + off, get32(h->block + off) + n);
    pthread_mutex_unlock(&h->mu);
}

void mesh_host_block_set_str(mesh_host_block *h, size_t off, size_t len, const char *s) {
    uint8_t b[256];
    size_t k = strlen(s);
    if (len > sizeof b) len = sizeof b;
    if (k > len - 1) k = len - 1;
    memset(b, 0, len);
    memcpy(b, s, k);
    put(h, off, b, len);
}

uint64_t mesh_host_block_uptime_ms(const mesh_host_block *h) { return (uint64_t)((mesh_mono_us() - h->started_us) / 1000); }

uint32_t mesh_host_block_get_u32(mesh_host_block *h, size_t off) {
    pthread_mutex_lock(&h->mu);
    uint32_t v = get32(h->block + off);
    pthread_mutex_unlock(&h->mu);
    return v;
}

float mesh_host_block_get_f32(mesh_host_block *h, size_t off) {
    uint32_t u = mesh_host_block_get_u32(h, off);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

void mesh_host_block_bytes(mesh_host_block *h, uint8_t out[MESH_SNAP_HOST]) {
    pthread_mutex_lock(&h->mu);
    memcpy(out, h->block, MESH_SNAP_HOST);
    pthread_mutex_unlock(&h->mu);
    uint64_t up = mesh_host_block_uptime_ms(h);
    for (int i = 0; i < 8; i++) out[MESH_S_UPTIME_MS + (size_t)i] = (uint8_t)(up >> (8 * i));
    mesh_net_counters_fill(&h->net, out);
}
