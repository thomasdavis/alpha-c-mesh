/* @ajax/mesh-rt-view: a bounds-checked byte window of the mailbox (a program's view, or
 * part of one), and the fault record a bad access leaves.
 *
 * Offsets are bytes from the window's start; u32/f32 accessors want 4-aligned
 * offsets, u64 8-aligned. An out-of-range access reads 0 or drops the write
 * and records FAULT_VIEW_BOUNDS | off << 8 in the caller's P_FAULT (meshd then
 * fails the program loudly). Ported from alpha-rust-mesh
 * crates/mesh-rt/src/ctx.rs (View, fault). */
#ifndef MESH_RT_VIEW_H
#define MESH_RT_VIEW_H

#include <stdbool.h>
#include <stdint.h>

#include "mesh/abi/control_block.h"
#include "mesh/abi/fault.h"
#include "mesh/rt/prim.h"

/* Records the first fault of a program (later ones keep the first). */
MESH_RT_INLINE void mesh_rt_fault(uint8_t *ctl, uint32_t code) {
    if (mesh_rt_ld_relaxed(ctl + MESH_P_FAULT) == 0) mesh_rt_st_relaxed(ctl + MESH_P_FAULT, code);
}

typedef struct {
    uint8_t *base;
    uint32_t len;
    /* The caller's control block, where a bounds fault is recorded. */
    uint8_t *ctl;
} mesh_view;

MESH_RT_INLINE bool mesh_view_ok(const mesh_view *v, uint32_t off, uint32_t n) {
    bool fine = (uint64_t)off + n <= v->len;
    if (!fine) mesh_rt_fault(v->ctl, MESH_FAULT_VIEW_BOUNDS | off << 8);
    return fine;
}
MESH_RT_INLINE uint32_t mesh_view_len(mesh_view v) { return v.len; }
MESH_RT_INLINE bool mesh_view_is_empty(mesh_view v) { return v.len == 0; }
/* The raw base pointer (unchecked access, e.g. bulk loops). */
MESH_RT_INLINE uint8_t *mesh_view_ptr(mesh_view v) { return v.base; }
MESH_RT_INLINE uint32_t mesh_view_u32(mesh_view v, uint32_t off) { return mesh_view_ok(&v, off, 4) ? mesh_rt_rd(v.base + off) : 0; }
MESH_RT_INLINE float mesh_view_f32(mesh_view v, uint32_t off) { return mesh_f32_from_bits(mesh_view_u32(v, off)); }
MESH_RT_INLINE uint64_t mesh_view_u64(mesh_view v, uint32_t off) { return mesh_view_ok(&v, off, 8) ? mesh_rt_rd64(v.base + off) : 0; }
MESH_RT_INLINE uint8_t mesh_view_u8(mesh_view v, uint32_t off) {
    return mesh_view_ok(&v, off, 1) ? *(const volatile uint8_t *)(v.base + off) : 0;
}
MESH_RT_INLINE void mesh_view_set_u32(mesh_view v, uint32_t off, uint32_t x) { if (mesh_view_ok(&v, off, 4)) mesh_rt_wr(v.base + off, x); }
MESH_RT_INLINE void mesh_view_set_f32(mesh_view v, uint32_t off, float x) { mesh_view_set_u32(v, off, mesh_f32_bits(x)); }
MESH_RT_INLINE void mesh_view_set_u64(mesh_view v, uint32_t off, uint64_t x) { if (mesh_view_ok(&v, off, 8)) mesh_rt_wr64(v.base + off, x); }
MESH_RT_INLINE void mesh_view_set_u8(mesh_view v, uint32_t off, uint8_t x) { if (mesh_view_ok(&v, off, 1)) mesh_rt_wrb(v.base + off, x); }
/* A sub-window [off, off + len): false (and a fault) when out of range. */
MESH_RT_INLINE bool mesh_view_sub(mesh_view v, uint32_t off, uint32_t len, mesh_view *out) {
    if (!mesh_view_ok(&v, off, len)) return false;
    *out = (mesh_view){v.base + off, len, v.ctl};
    return true;
}

#endif
