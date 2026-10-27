/* @ajax/mesh-rt-ptx: the device primitives of mesh-rt on NVIDIA GPUs, as inline PTX.
 *
 * Special registers, system-scope memory ordering, atomics, warp primitives
 * and the block barrier: everything the mesh shares with the host goes
 * through these (or the volatile payload accessors, ordered by an
 * acquire/release pair around them). Each asm has a "memory" clobber, as
 * Rust's asm! without `nomem` is a compiler barrier.
 *
 * Only for nvptx64 (clang --target=nvptx64-nvidia-cuda); elsewhere this header
 * is empty, and @ajax/mesh-rt-sim provides the same functions on the CPU.
 * Ported from alpha-rust-mesh crates/mesh-rt/src/ptx.rs. */
#ifndef MESH_RT_PTX_H
#define MESH_RT_PTX_H

#if defined(__NVPTX__)

#include <stdbool.h>
#include <stdint.h>

#ifndef MESH_RT_INLINE
#define MESH_RT_INLINE static inline __attribute__((always_inline))
#endif

/* The node's dynamic shared memory: RT_SHARED_BYTES for the runtime, then the
 * program's `shared_bytes` (meshd launches every block with both). */
extern __attribute__((address_space(3), aligned(16))) unsigned char mesh_smem[];

MESH_RT_INLINE uint32_t mesh_rt_smid(void) { uint32_t r; __asm__ __volatile__("mov.u32 %0, %%smid;" : "=r"(r)::"memory"); return r; }
/* The thread's index in its block (node): 0..32 x warps. */
MESH_RT_INLINE uint32_t mesh_rt_tid(void) { uint32_t r; __asm__ __volatile__("mov.u32 %0, %%tid.x;" : "=r"(r)::"memory"); return r; }
/* The thread's lane in its warp (0..32). */
MESH_RT_INLINE uint32_t mesh_rt_lane(void) { return mesh_rt_tid() & 31u; }
/* The launch's dynamic shared memory bytes (RT_SHARED_BYTES + the manifest's `shared_bytes`). */
MESH_RT_INLINE uint32_t mesh_rt_dyn_smem(void) { uint32_t r; __asm__ __volatile__("mov.u32 %0, %%dynamic_smem_size;" : "=r"(r)::"memory"); return r; }
/* Threads per block (32 x the manifest's `warps`). */
MESH_RT_INLINE uint32_t mesh_rt_ntid(void) { uint32_t r; __asm__ __volatile__("mov.u32 %0, %%ntid.x;" : "=r"(r)::"memory"); return r; }
MESH_RT_INLINE uint32_t mesh_rt_ctaid(void) { uint32_t r; __asm__ __volatile__("mov.u32 %0, %%ctaid.x;" : "=r"(r)::"memory"); return r; }
MESH_RT_INLINE uint64_t mesh_rt_gtimer(void) { uint64_t r; __asm__ __volatile__("mov.u64 %0, %%globaltimer;" : "=l"(r)::"memory"); return r; }

MESH_RT_INLINE uint32_t mesh_rt_ld_acquire(const uint8_t *p) {
    uint32_t r; __asm__ __volatile__("ld.acquire.sys.global.u32 %0, [%1];" : "=r"(r) : "l"(p) : "memory"); return r;
}
MESH_RT_INLINE uint64_t mesh_rt_ld_acquire64(const uint8_t *p) {
    uint64_t r; __asm__ __volatile__("ld.acquire.sys.global.u64 %0, [%1];" : "=l"(r) : "l"(p) : "memory"); return r;
}
MESH_RT_INLINE uint32_t mesh_rt_ld_relaxed(const uint8_t *p) {
    uint32_t r; __asm__ __volatile__("ld.relaxed.sys.global.u32 %0, [%1];" : "=r"(r) : "l"(p) : "memory"); return r;
}
MESH_RT_INLINE uint64_t mesh_rt_ld_relaxed64(const uint8_t *p) {
    uint64_t r; __asm__ __volatile__("ld.relaxed.sys.global.u64 %0, [%1];" : "=l"(r) : "l"(p) : "memory"); return r;
}
MESH_RT_INLINE void mesh_rt_st_release(uint8_t *p, uint32_t v) {
    __asm__ __volatile__("st.release.sys.global.u32 [%0], %1;" ::"l"(p), "r"(v) : "memory");
}
MESH_RT_INLINE void mesh_rt_st_release64(uint8_t *p, uint64_t v) {
    __asm__ __volatile__("st.release.sys.global.u64 [%0], %1;" ::"l"(p), "l"(v) : "memory");
}
MESH_RT_INLINE void mesh_rt_st_relaxed(uint8_t *p, uint32_t v) {
    __asm__ __volatile__("st.relaxed.sys.global.u32 [%0], %1;" ::"l"(p), "r"(v) : "memory");
}
MESH_RT_INLINE uint32_t mesh_rt_atom_add(uint8_t *p, uint32_t v) {
    uint32_t r; __asm__ __volatile__("atom.add.relaxed.sys.global.u32 %0, [%1], %2;" : "=r"(r) : "l"(p), "r"(v) : "memory"); return r;
}
MESH_RT_INLINE uint32_t mesh_rt_atom_or(uint8_t *p, uint32_t v) {
    uint32_t r; __asm__ __volatile__("atom.or.relaxed.sys.global.b32 %0, [%1], %2;" : "=r"(r) : "l"(p), "r"(v) : "memory"); return r;
}
MESH_RT_INLINE uint32_t mesh_rt_atom_and(uint8_t *p, uint32_t v) {
    uint32_t r; __asm__ __volatile__("atom.and.relaxed.sys.global.b32 %0, [%1], %2;" : "=r"(r) : "l"(p), "r"(v) : "memory"); return r;
}
MESH_RT_INLINE uint32_t mesh_rt_atom_cas(uint8_t *p, uint32_t cmp, uint32_t desired) {
    uint32_t r;
    __asm__ __volatile__("atom.cas.relaxed.sys.global.b32 %0, [%1], %2, %3;" : "=r"(r) : "l"(p), "r"(cmp), "r"(desired) : "memory");
    return r;
}
MESH_RT_INLINE void mesh_rt_red_add(uint8_t *p, uint32_t v) {
    __asm__ __volatile__("red.relaxed.sys.global.add.u32 [%0], %1;" ::"l"(p), "r"(v) : "memory");
}
MESH_RT_INLINE void mesh_rt_fence_sc(void) { __asm__ __volatile__("fence.sc.sys;" ::: "memory"); }
MESH_RT_INLINE void mesh_rt_fence_acq_rel(void) { __asm__ __volatile__("fence.acq_rel.sys;" ::: "memory"); }
MESH_RT_INLINE void mesh_rt_nanosleep(uint32_t ns) { __asm__ __volatile__("nanosleep.u32 %0;" ::"r"(ns) : "memory"); }
MESH_RT_INLINE void mesh_rt_warp_sync(void) { __asm__ __volatile__("bar.warp.sync -1;" ::: "memory"); }
/* The block barrier (`barrier.sync 0`): every thread of the node waits for
 * every other, and memory operations before it are ordered before those after
 * it for the whole block. Every thread must reach it. Not `bar.sync`
 * (= barrier.sync.aligned, which needs every thread of a warp at the same
 * instruction): inline asm is not convergent to LLVM, which may duplicate it
 * into both arms of a branch, and an aligned barrier reached that way hangs. */
MESH_RT_INLINE void mesh_rt_bar_sync(void) { __asm__ __volatile__("barrier.sync 0;" ::: "memory"); }
/* The generic address of the node's dynamic shared memory. */
MESH_RT_INLINE uint8_t *mesh_rt_smem_base(void) { return (uint8_t *)mesh_smem; }

MESH_RT_INLINE uint32_t mesh_rt_shfl_idx(uint32_t v, uint32_t src) {
    uint32_t r; __asm__ __volatile__("shfl.sync.idx.b32 %0, %1, %2, 31, -1;" : "=r"(r) : "r"(v), "r"(src) : "memory"); return r;
}
MESH_RT_INLINE uint32_t mesh_rt_shfl_bfly(uint32_t v, uint32_t m) {
    uint32_t r; __asm__ __volatile__("shfl.sync.bfly.b32 %0, %1, %2, 31, -1;" : "=r"(r) : "r"(v), "r"(m) : "memory"); return r;
}
/* Lane l gets lane l - d's value (lanes below d keep their own). */
MESH_RT_INLINE uint32_t mesh_rt_shfl_up(uint32_t v, uint32_t d) {
    uint32_t r; __asm__ __volatile__("shfl.sync.up.b32 %0, %1, %2, 0, -1;" : "=r"(r) : "r"(v), "r"(d) : "memory"); return r;
}
/* Bits of the converged lanes whose predicate is true. */
MESH_RT_INLINE uint32_t mesh_rt_ballot(bool p) {
    uint32_t r;
    __asm__ __volatile__("{ .reg .pred q; setp.ne.u32 q, %1, 0; vote.sync.ballot.b32 %0, q, -1; }" : "=r"(r) : "r"((uint32_t)p) : "memory");
    return r;
}

/* Payload access, ordered by the acquire/release around it. */
MESH_RT_INLINE uint32_t mesh_rt_rd(const uint8_t *p) { return *(const volatile uint32_t *)p; }
MESH_RT_INLINE uint64_t mesh_rt_rd64(const uint8_t *p) { return *(const volatile uint64_t *)p; }
MESH_RT_INLINE void mesh_rt_wr(uint8_t *p, uint32_t v) { *(volatile uint32_t *)p = v; }
MESH_RT_INLINE void mesh_rt_wrb(uint8_t *p, uint8_t v) { *(volatile uint8_t *)p = v; }
MESH_RT_INLINE void mesh_rt_wr64(uint8_t *p, uint64_t v) { *(volatile uint64_t *)p = v; }
/* A panic, or a broken runtime invariant: faults the context (meshd sees the error). */
MESH_RT_INLINE _Noreturn void mesh_rt_trap(void) {
    __asm__ __volatile__("trap;" ::: "memory");
    __builtin_unreachable();
}

#endif /* __NVPTX__ */
#endif
