/* mps.rs's frame, read_frame and u32_at. */
#include "mesh/mps_wire.h"
#include "mesh/text.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void mesh_mps_frame(uint8_t op, const uint8_t *body, size_t n, uint8_t **out, size_t *out_len) {
    uint8_t *f = mesh_alloc(n + 5);
    uint32_t len = (uint32_t)(n + 1);
    for (int i = 0; i < 4; i++) f[i] = (uint8_t)(len >> (8 * i));
    f[4] = op;
    if (n) memcpy(f + 5, body, n);
    *out = f;
    *out_len = n + 5;
}

static bool write_all(int fd, const uint8_t *b, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t k = write(fd, b + off, n - off);
        if (k > 0) off += (size_t)k;
        else if (k < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

bool mesh_mps_write_frame(int fd, uint8_t op, const uint8_t *body, size_t n) {
    uint8_t *f;
    size_t fn;
    mesh_mps_frame(op, body, n, &f, &fn);
    bool ok = write_all(fd, f, fn);
    free(f);
    return ok;
}

static bool read_exact(int fd, uint8_t *b, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t k = read(fd, b + off, n - off);
        if (k > 0) off += (size_t)k;
        else if (k < 0 && errno == EINTR) continue;
        else return false;
    }
    return true;
}

bool mesh_mps_read_frame(int fd, uint8_t *op, uint8_t **body, size_t *n) {
    uint8_t h[4];
    if (!read_exact(fd, h, 4)) return false;
    uint32_t len = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
    if (len == 0 || len > MESH_W_FRAME_MAX) return false;
    uint8_t *b = mesh_alloc(len);
    if (!read_exact(fd, b, len)) {
        free(b);
        return false;
    }
    *op = b[0];
    memmove(b, b + 1, len - 1);
    *body = b;
    *n = len - 1;
    return true;
}

uint32_t mesh_mps_u32_at(const uint8_t *b, size_t n, size_t o) {
    if (o + 4 > n) return 0;
    return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24;
}

uint64_t mesh_mps_u64_at(const uint8_t *b, size_t n, size_t o) {
    if (o + 8 > n) return 0;
    return (uint64_t)mesh_mps_u32_at(b, n, o) | (uint64_t)mesh_mps_u32_at(b, n, o + 4) << 32;
}

void mesh_mps_put_bytes(uint8_t **b, size_t *n, const void *p, size_t k) {
    *b = mesh_realloc(*b, *n + k);
    if (k) memcpy(*b + *n, p, k);
    *n += k;
}

void mesh_mps_put_u32(uint8_t **b, size_t *n, uint32_t v) {
    uint8_t w[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    mesh_mps_put_bytes(b, n, w, 4);
}

void mesh_mps_put_u64(uint8_t **b, size_t *n, uint64_t v) {
    mesh_mps_put_u32(b, n, (uint32_t)v);
    mesh_mps_put_u32(b, n, (uint32_t)(v >> 32));
}
