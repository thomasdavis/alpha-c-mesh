/* @ajax/mesh-rt-block: the block collectives of a multi-warp node in a fixed order (barrier,
 * broadcast, reductions, scans), the fixed f32 operations, and typed windows
 * of the node's shared memory.
 *
 * Every thread of the node must call a collective, the same ones in the same
 * order: never after a branch the threads may take differently, never after
 * some threads returned from the task. Warps with nothing to do still call.
 *
 * A fixed order: the float results are bitwise the same on every GPU and in
 * the CPU reference (mesh_ref::block), because the combination tree is fixed
 * and every f32 op is correctly rounded, unfused and FTZ:
 *  - a reduction: within each warp a butterfly (lane l combines with lane
 *    l ^ m for m = 16, 8, 4, 2, 1), then the same butterfly over the 32 warp
 *    results, missing warps filled with the identity (sum: -0.0). The tree is
 *    the one over 1024 leaves whatever `warps` is;
 *  - a scan (inclusive): within each warp Hillis-Steele (lane l adds lane
 *    l - d for d = 1, 2, 4, 8, 16, the earlier operand first), the warp totals
 *    scanned the same way, then each warp after the first combines its prefix
 *    with its own scan (prefix + x); exclusive: the thread before's inclusive
 *    value within a warp, a warp's first thread the prefix (warp 0: identity);
 *  - max over f32 is the IEEE total order on the bits: one of the inputs,
 *    unchanged. u32/u64 sums wrap.
 * The scratch is the runtime's shared area (RT_SHARED_BYTES): each collective
 * starts with a barrier, so they may follow each other directly.
 *
 * Rust's generic `Word` and closures become one function per type (u32, f32,
 * u64) taking the operation as a function pointer; every call is inlined, so
 * the pointer is a constant and the operation is inlined too.
 * Ported from alpha-rust-mesh crates/mesh-rt/src/block.rs. */
#ifndef MESH_RT_BLOCK_H
#define MESH_RT_BLOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "mesh/abi/base.h"
#include "mesh/abi/fault.h"
#include "mesh/rt/prim.h"
#include "mesh/rt/view.h"

/* The runtime's shared area (RT_SHARED_BYTES), byte offsets. The node loop's
 * ticket descriptor (thread 0 writes it, the block reads it after a barrier). */
#define MESH_RT_DESC 0x00u
#define MESH_RT_D_T 0x00u
#define MESH_RT_D_GO 0x04u
#define MESH_RT_D_OP 0x08u
#define MESH_RT_D_ARG 0x0cu
#define MESH_RT_D_CSLOT 0x10u
/* One 8-byte word per warp (reductions, scans). */
#define MESH_RT_RED 0x40u
/* The broadcast word. */
#define MESH_RT_BCAST (MESH_RT_RED + 8u * 32u)
_Static_assert(MESH_RT_DESC + 0x20u <= MESH_RT_RED && MESH_RT_BCAST + 8u <= MESH_RT_SHARED_BYTES,
               "the runtime's shared area holds the descriptor, the warp words and the broadcast word");

/* ---- the fixed operations (mesh_ref::block has the same) */
/* f32 sum's identity: -0.0 (x + -0.0 == x for every x, +0 and -0 included). */
#define MESH_RT_SUM_F32_ID (-0.0f)
/* f32 max's identity: the least value of the total order (a negative NaN, all bits set). */
#define MESH_RT_MAX_F32_ID_BITS 0xffffffffu
MESH_RT_INLINE uint32_t mesh_rt_order_key(uint32_t b) { return (b & 0x80000000u) ? ~b : (b | 0x80000000u); }
/* The larger of a and b in the IEEE total order of their bits (one of them, unchanged). */
MESH_RT_INLINE float mesh_rt_max_f32(float a, float b) {
    return mesh_rt_order_key(mesh_f32_bits(b)) > mesh_rt_order_key(mesh_f32_bits(a)) ? b : a;
}
MESH_RT_INLINE float mesh_rt_add_f32(float a, float b) { return a + b; }
MESH_RT_INLINE uint32_t mesh_rt_add_u32(uint32_t a, uint32_t b) { return a + b; }
MESH_RT_INLINE uint32_t mesh_rt_max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }
MESH_RT_INLINE uint32_t mesh_rt_min_u32(uint32_t a, uint32_t b) { return a < b ? a : b; }
MESH_RT_INLINE uint64_t mesh_rt_add_u64(uint64_t a, uint64_t b) { return a + b; }
MESH_RT_INLINE uint64_t mesh_rt_max_u64(uint64_t a, uint64_t b) { return a > b ? a : b; }
MESH_RT_INLINE uint64_t mesh_rt_min_u64(uint64_t a, uint64_t b) { return a < b ? a : b; }

typedef uint32_t (*mesh_rt_op_u32)(uint32_t, uint32_t);
typedef float (*mesh_rt_op_f32)(float, float);
typedef uint64_t (*mesh_rt_op_u64)(uint64_t, uint64_t);

/* ---- moving words between lanes and through shared memory (Rust's Word) */
MESH_RT_INLINE uint32_t mesh_rt_word_u32_xor(uint32_t v, uint32_t m) { return mesh_rt_shfl_bfly(v, m); }
MESH_RT_INLINE uint32_t mesh_rt_word_u32_up(uint32_t v, uint32_t d) { return mesh_rt_shfl_up(v, d); }
MESH_RT_INLINE uint32_t mesh_rt_word_u32_idx(uint32_t v, uint32_t s) { return mesh_rt_shfl_idx(v, s); }
MESH_RT_INLINE uint32_t mesh_rt_word_u32_load(const uint8_t *p) { return *(const volatile uint32_t *)(const volatile void *)p; }
MESH_RT_INLINE void mesh_rt_word_u32_store(uint32_t v, uint8_t *p) { *(volatile uint32_t *)(volatile void *)p = v; }

MESH_RT_INLINE float mesh_rt_word_f32_xor(float v, uint32_t m) { return mesh_f32_from_bits(mesh_rt_shfl_bfly(mesh_f32_bits(v), m)); }
MESH_RT_INLINE float mesh_rt_word_f32_up(float v, uint32_t d) { return mesh_f32_from_bits(mesh_rt_shfl_up(mesh_f32_bits(v), d)); }
MESH_RT_INLINE float mesh_rt_word_f32_idx(float v, uint32_t s) { return mesh_f32_from_bits(mesh_rt_shfl_idx(mesh_f32_bits(v), s)); }
MESH_RT_INLINE float mesh_rt_word_f32_load(const uint8_t *p) { return mesh_f32_from_bits(mesh_rt_word_u32_load(p)); }
MESH_RT_INLINE void mesh_rt_word_f32_store(float v, uint8_t *p) { mesh_rt_word_u32_store(mesh_f32_bits(v), p); }

MESH_RT_INLINE uint64_t mesh_rt_word_u64_xor(uint64_t v, uint32_t m) {
    return (uint64_t)mesh_rt_shfl_bfly((uint32_t)v, m) | (uint64_t)mesh_rt_shfl_bfly((uint32_t)(v >> 32), m) << 32;
}
MESH_RT_INLINE uint64_t mesh_rt_word_u64_up(uint64_t v, uint32_t d) {
    return (uint64_t)mesh_rt_shfl_up((uint32_t)v, d) | (uint64_t)mesh_rt_shfl_up((uint32_t)(v >> 32), d) << 32;
}
MESH_RT_INLINE uint64_t mesh_rt_word_u64_idx(uint64_t v, uint32_t s) {
    return (uint64_t)mesh_rt_shfl_idx((uint32_t)v, s) | (uint64_t)mesh_rt_shfl_idx((uint32_t)(v >> 32), s) << 32;
}
MESH_RT_INLINE uint64_t mesh_rt_word_u64_load(const uint8_t *p) { return *(const volatile uint64_t *)(const volatile void *)p; }
MESH_RT_INLINE void mesh_rt_word_u64_store(uint64_t v, uint8_t *p) { *(volatile uint64_t *)(volatile void *)p = v; }

/* The collectives for one word type T (suffix S): warp_reduce, warp_scan,
 * block reduce, scan and broadcast. */
#define MESH_RT_COLLECTIVES(T, S)                                                                                  \
    /* Butterfly over the warp's 32 lanes (m = 16, 8, 4, 2, 1); every lane ends with the same value when op is \
     * commutative. Warp-collective. */                                                                          \
    MESH_RT_INLINE T mesh_rt_warp_reduce_##S(T v, mesh_rt_op_##S op) {                                               \
        for (uint32_t m = 16; m > 0; m >>= 1) v = op(v, mesh_rt_word_##S##_xor(v, m));                           \
        return v;                                                                                              \
    }                                                                                                          \
    /* Inclusive Hillis-Steele scan over the warp's lanes (d = 1, 2, 4, 8, 16: lane l >= d computes            \
     * op(lane l - d's value, its own)). Warp-collective. */                                                   \
    MESH_RT_INLINE T mesh_rt_warp_scan_##S(T v, mesh_rt_op_##S op) {                                                 \
        uint32_t l = mesh_rt_lane();                                                                           \
        for (uint32_t d = 1; d < 32; d <<= 1) {                                                                \
            T y = mesh_rt_word_##S##_up(v, d);                                                                    \
            if (l >= d) v = op(y, v);                                                                          \
        }                                                                                                      \
        return v;                                                                                              \
    }                                                                                                          \
    /* Block reduction in the fixed order: every thread gets the result. */                                    \
    MESH_RT_INLINE T mesh_rt_block_reduce_##S(T v, T id, mesh_rt_op_##S op) {                                       \
        uint32_t l = mesh_rt_lane(), w = mesh_rt_tid() >> 5, warps = mesh_rt_ntid() >> 5;                     \
        uint8_t *s = mesh_rt_smem_base();                                                                     \
        mesh_rt_bar_sync();                                                                                    \
        T r = mesh_rt_warp_reduce_##S(v, op);                                                                     \
        if (l == 0) mesh_rt_word_##S##_store(r, s + MESH_RT_RED + 8u * w);                                       \
        mesh_rt_bar_sync();                                                                                    \
        T x = l < warps ? mesh_rt_word_##S##_load(s + MESH_RT_RED + 8u * l) : id;                                \
        return mesh_rt_warp_reduce_##S(x, op);                                                                    \
    }                                                                                                          \
    /* Block scan in the fixed order: inclusive, or exclusive (thread 0: id). */                               \
    MESH_RT_INLINE T mesh_rt_block_scan_##S(T v, T id, mesh_rt_op_##S op, bool inclusive) {                         \
        uint32_t l = mesh_rt_lane(), w = mesh_rt_tid() >> 5, warps = mesh_rt_ntid() >> 5;                     \
        uint8_t *s = mesh_rt_smem_base();                                                                     \
        mesh_rt_bar_sync();                                                                                    \
        T inc = mesh_rt_warp_scan_##S(v, op);                                                                     \
        if (l == 31) mesh_rt_word_##S##_store(inc, s + MESH_RT_RED + 8u * w);                                    \
        mesh_rt_bar_sync();                                                                                    \
        T t = l < warps ? mesh_rt_word_##S##_load(s + MESH_RT_RED + 8u * l) : id;                                \
        T ts = mesh_rt_warp_scan_##S(t, op);                                                                      \
        /* The total of warps 0..w-1 (lane w - 1's inclusive scan of the totals). */                          \
        T prefix = mesh_rt_word_##S##_idx(ts, (w - 1u) & 31u);                                                   \
        if (w != 0) inc = op(prefix, inc);                                                                     \
        if (inclusive) return inc;                                                                             \
        T up = mesh_rt_word_##S##_up(inc, 1);                                                                     \
        if (l > 0) return up;                                                                                  \
        return w == 0 ? id : prefix;                                                                           \
    }                                                                                                          \
    /* Thread src's v on every thread of the block. */                                                         \
    MESH_RT_INLINE T mesh_rt_block_broadcast_##S(T v, uint32_t src) {                                            \
        uint8_t *s = mesh_rt_smem_base();                                                                     \
        mesh_rt_bar_sync();                                                                                    \
        if (mesh_rt_tid() == src) mesh_rt_word_##S##_store(v, s + MESH_RT_BCAST);                                \
        mesh_rt_bar_sync();                                                                                    \
        return mesh_rt_word_##S##_load(s + MESH_RT_BCAST);                                                       \
    }

MESH_RT_COLLECTIVES(uint32_t, u32)
MESH_RT_COLLECTIVES(float, f32)
MESH_RT_COLLECTIVES(uint64_t, u64)

/* ---- a typed window of the node's shared memory (Ctx::shared): len
 * elements, the same memory for every thread of the node and private to it.
 * Ordering between threads is the program's: write, barrier, read. get/set
 * are bounds checked (out of range: 0 / dropped, and FAULT_SHARED_BOUNDS | i << 8);
 * the _unchecked ones and the raw pointer are not. */
typedef struct {
    uint8_t *base;
    uint32_t len; /* elements */
    uint8_t *ctl;
} mesh_shared;

#define MESH_RT_SHARED_ACCESS(T, S)                                                                               \
    MESH_RT_INLINE T mesh_shared_get_##S(mesh_shared sh, uint32_t i) {                                         \
        if (i < sh.len) return ((T *)(void *)sh.base)[i];                                                     \
        mesh_rt_fault(sh.ctl, MESH_FAULT_SHARED_BOUNDS | i << 8);                                              \
        return (T)0;                                                                                          \
    }                                                                                                          \
    MESH_RT_INLINE void mesh_shared_set_##S(mesh_shared sh, uint32_t i, T v) {                                 \
        if (i < sh.len) ((T *)(void *)sh.base)[i] = v;                                                        \
        else mesh_rt_fault(sh.ctl, MESH_FAULT_SHARED_BOUNDS | i << 8);                                         \
    }                                                                                                          \
    MESH_RT_INLINE T mesh_shared_get_unchecked_##S(mesh_shared sh, uint32_t i) { return ((T *)(void *)sh.base)[i]; } \
    MESH_RT_INLINE void mesh_shared_set_unchecked_##S(mesh_shared sh, uint32_t i, T v) { ((T *)(void *)sh.base)[i] = v; } \
    MESH_RT_INLINE T *mesh_shared_ptr_##S(mesh_shared sh) { return (T *)(void *)sh.base; }

MESH_RT_SHARED_ACCESS(uint32_t, u32)
MESH_RT_SHARED_ACCESS(float, f32)
MESH_RT_SHARED_ACCESS(uint64_t, u64)

#endif
