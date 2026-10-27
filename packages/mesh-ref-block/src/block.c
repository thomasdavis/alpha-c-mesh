/* The block collectives on the CPU (mesh-ref block.rs), generated for f32, u32 and u64. */
#include "mesh/ref/block.h"

#include "mesh/ftz.h"

float mesh_block_sum_f32_id(void) { return mesh_ftz_from_bits(MESH_BLOCK_SUM_F32_ID_BITS); }
float mesh_block_max_f32_id(void) { return mesh_ftz_from_bits(MESH_BLOCK_MAX_F32_ID_BITS); }

static uint32_t order_key(uint32_t b) { return (b & 0x80000000u) ? ~b : (b | 0x80000000u); }

float mesh_block_max_f32(float a, float b) {
    return order_key(mesh_ftz_bits(b)) > order_key(mesh_ftz_bits(a)) ? b : a;
}

float mesh_block_add_f32(float a, float b) { return mesh_ftz_add(a, b); }
uint32_t mesh_block_add_u32(uint32_t a, uint32_t b) { return a + b; }
uint64_t mesh_block_add_u64(uint64_t a, uint64_t b) { return a + b; }

uint32_t mesh_block_mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

void mesh_block_test_values(uint32_t seed, uint32_t t, float *f, uint32_t *u, uint64_t *w) {
    uint32_t h = mesh_block_mix((seed * 0x9e3779b9u) ^ t);
    uint32_t h2 = mesh_block_mix(h ^ 0x85ebca6bu);
    uint32_t h3 = mesh_block_mix(h2 ^ 0xc2b2ae35u);
    uint32_t bits;
    switch (h & 15) {
    case 0: bits = (h & 16) ? 0x80000000u : 0; break;                              /* a zero */
    case 1: bits = (h & 0x80000000u) | (h2 & 0x007fffffu) | 1; break;              /* subnormal */
    case 2: bits = (h & 0x80000000u) | (200u << 23) | (h2 & 0x007fffffu); break;   /* ~2^73 */
    default: bits = (h & 0x80000000u) | ((112 + (h2 >> 28)) << 23) | (h3 & 0x007fffffu); /* 2^-15..2^0 */
    }
    *f = mesh_ftz_from_bits(bits);
    *u = h2;
    *w = (uint64_t)h3 << 32 | h;
}

#define MESH_BLOCK_DEFINE(T, S)                                                                    \
    void mesh_block_warp_reduce_##S(T v[32], mesh_block_op_##S op) {                               \
        for (unsigned m = 16; m >= 1; m >>= 1) {                                                   \
            T o[32];                                                                               \
            for (unsigned l = 0; l < 32; l++) o[l] = v[l];                                         \
            for (unsigned l = 0; l < 32; l++) v[l] = op(o[l], o[l ^ m]);                           \
        }                                                                                          \
    }                                                                                              \
    void mesh_block_warp_scan_##S(T v[32], mesh_block_op_##S op) {                                 \
        for (unsigned d = 1; d <= 16; d <<= 1) {                                                   \
            T o[32];                                                                               \
            for (unsigned l = 0; l < 32; l++) o[l] = v[l];                                         \
            for (unsigned l = d; l < 32; l++) v[l] = op(o[l - d], o[l]);                           \
        }                                                                                          \
    }                                                                                              \
    T mesh_block_reduce_##S(const T *vals, size_t n, T id, mesh_block_op_##S op) {                 \
        if (n == 0 || n % 32 != 0 || n > 1024) return id;                                          \
        size_t warps = n / 32;                                                                     \
        T tot[32];                                                                                 \
        for (size_t w = 0; w < 32; w++) tot[w] = id;                                               \
        for (size_t w = 0; w < warps; w++) {                                                       \
            T v[32];                                                                               \
            for (size_t l = 0; l < 32; l++) v[l] = vals[32 * w + l];                               \
            mesh_block_warp_reduce_##S(v, op);                                                     \
            tot[w] = v[0];                                                                         \
        }                                                                                          \
        mesh_block_warp_reduce_##S(tot, op);                                                       \
        return tot[0];                                                                             \
    }                                                                                              \
    bool mesh_block_scan_##S(const T *vals, size_t n, T id, mesh_block_op_##S op, bool inclusive,  \
                             T *out) {                                                             \
        if (n == 0 || n % 32 != 0 || n > 1024) return false;                                       \
        size_t warps = n / 32;                                                                     \
        T local[32][32], tot[32];                                                                  \
        for (size_t w = 0; w < 32; w++) tot[w] = id;                                               \
        for (size_t w = 0; w < warps; w++) {                                                       \
            for (size_t l = 0; l < 32; l++) local[w][l] = vals[32 * w + l];                        \
            mesh_block_warp_scan_##S(local[w], op);                                                \
            tot[w] = local[w][31];                                                                 \
        }                                                                                          \
        mesh_block_warp_scan_##S(tot, op);                                                         \
        for (size_t w = 0; w < warps; w++) {                                                       \
            T prefix = tot[(w + 31) % 32], inc[32];                                                \
            for (size_t l = 0; l < 32; l++) inc[l] = w == 0 ? local[w][l] : op(prefix, local[w][l]); \
            for (size_t l = 0; l < 32; l++)                                                        \
                out[32 * w + l] = inclusive ? inc[l] : l > 0 ? inc[l - 1] : w == 0 ? id : prefix;  \
        }                                                                                          \
        return true;                                                                               \
    }

MESH_BLOCK_DEFINE(float, f32)
MESH_BLOCK_DEFINE(uint32_t, u32)
MESH_BLOCK_DEFINE(uint64_t, u64)
