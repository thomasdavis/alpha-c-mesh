/* @ajax/mesh-rt-sim: mesh-rt's device primitives on the CPU, and a launcher that runs a
 * kernel's nodes as threads, so the same C program can be tested without a GPU.
 *
 * Each node is `warps` x 32 POSIX threads with a shared-memory buffer; the
 * block barrier is a barrier over the node's threads, and every warp
 * collective (shuffle, ballot, warp barrier) is an exchange through a
 * per-warp array between two barriers over its 32 lanes, so programs that
 * follow mesh-rt's collective rules behave as on the GPU. Memory ordering is
 * C11 atomics with the same orders; the global timer is CLOCK_MONOTONIC.
 * Every simulated thread runs with the f32 flush-to-zero mode of the cluster
 * float mode (x86 MXCSR FTZ|DAZ, AArch64 FPCR.FZ), as the PTX's .ftz ops do.
 *
 * Empty on nvptx64 (@ajax/mesh-rt-ptx has the real ones there). */
#ifndef MESH_RT_SIM_H
#define MESH_RT_SIM_H

#if !defined(__NVPTX__)

#include <stdbool.h>
#include <stdint.h>

#ifndef MESH_RT_INLINE
#define MESH_RT_INLINE static inline __attribute__((always_inline))
#endif

/* The primitives: the same names and meaning as @ajax/mesh-rt-ptx. */
uint32_t mesh_rt_smid(void);
uint32_t mesh_rt_tid(void);
uint32_t mesh_rt_lane(void);
uint32_t mesh_rt_dyn_smem(void);
uint32_t mesh_rt_ntid(void);
uint32_t mesh_rt_ctaid(void);
uint64_t mesh_rt_gtimer(void);
uint32_t mesh_rt_ld_acquire(const uint8_t *p);
uint64_t mesh_rt_ld_acquire64(const uint8_t *p);
uint32_t mesh_rt_ld_relaxed(const uint8_t *p);
uint64_t mesh_rt_ld_relaxed64(const uint8_t *p);
void mesh_rt_st_release(uint8_t *p, uint32_t v);
void mesh_rt_st_release64(uint8_t *p, uint64_t v);
void mesh_rt_st_relaxed(uint8_t *p, uint32_t v);
uint32_t mesh_rt_atom_add(uint8_t *p, uint32_t v);
uint32_t mesh_rt_atom_or(uint8_t *p, uint32_t v);
uint32_t mesh_rt_atom_and(uint8_t *p, uint32_t v);
uint32_t mesh_rt_atom_cas(uint8_t *p, uint32_t cmp, uint32_t desired);
void mesh_rt_red_add(uint8_t *p, uint32_t v);
void mesh_rt_fence_sc(void);
void mesh_rt_fence_acq_rel(void);
void mesh_rt_nanosleep(uint32_t ns);
void mesh_rt_warp_sync(void);
void mesh_rt_bar_sync(void);
uint8_t *mesh_rt_smem_base(void);
uint32_t mesh_rt_shfl_idx(uint32_t v, uint32_t src);
uint32_t mesh_rt_shfl_bfly(uint32_t v, uint32_t m);
uint32_t mesh_rt_shfl_up(uint32_t v, uint32_t d);
uint32_t mesh_rt_ballot(bool p);
_Noreturn void mesh_rt_trap(void);

MESH_RT_INLINE uint32_t mesh_rt_rd(const uint8_t *p) { return *(const volatile uint32_t *)(const volatile void *)p; }
MESH_RT_INLINE uint64_t mesh_rt_rd64(const uint8_t *p) { return *(const volatile uint64_t *)(const volatile void *)p; }
MESH_RT_INLINE void mesh_rt_wr(uint8_t *p, uint32_t v) { *(volatile uint32_t *)(volatile void *)p = v; }
MESH_RT_INLINE void mesh_rt_wrb(uint8_t *p, uint8_t v) { *(volatile uint8_t *)p = v; }
MESH_RT_INLINE void mesh_rt_wr64(uint8_t *p, uint64_t v) { *(volatile uint64_t *)(volatile void *)p = v; }

/* ---- the launcher */

/* A kernel: `mesh_main` of a program compiled for the host (MESH_PROGRAM). */
typedef void (*mesh_sim_kernel)(uint8_t *mb, uint32_t slot);
typedef struct mesh_sim_launch mesh_sim_launch;

/* Starts `nodes` blocks of 32 x `warps` threads running kernel(mb, slot), each
 * node with RT_SHARED_BYTES + shared_bytes of zeroed shared memory (as meshd's
 * cuLaunchKernel). Returns at once; NULL if threads could not be created. */
mesh_sim_launch *mesh_sim_start(mesh_sim_kernel kernel, uint8_t *mb, uint32_t slot, uint32_t nodes, uint32_t warps,
                                uint32_t shared_bytes);
/* Waits until every thread of the launch has returned, then frees it. */
void mesh_sim_join(mesh_sim_launch *l);

#endif /* !__NVPTX__ */
#endif
