/* @ajax/mesh-abi-backend: Backend kinds (cuda, metal, wasm, native) and the H_BACKEND word.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_BACKEND_H
#define MESH_ABI_BACKEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* H_BACKEND's low byte.  CUDA: one resident kernel per program (mesh-rt,
 * PTX, nvJitLink).  Metal (Apple GPUs): no resident kernels -- a dispatch
 * cannot see host writes made while it runs -- so meshd's *lease* threads
 * play the node loop on the CPU: each takes a running program's published
 * tickets in order, runs them as one dispatch (a threadgroup of NODE_THREADS
 * per task, a SIMD-group: the warp), and completes them exactly as mesh-rt's
 * nodes do (results, marks, P_DONE, node records, trace).  The rings, the
 * publisher, control and snapshots are the same words on both. */
#define MESH_BACKEND_CUDA 0u
#define MESH_BACKEND_METAL 1u
/* WebAssembly on the host CPU (wasmtime embedded in meshd; mesh-host
 * `wasm.rs`): an *executor*, not a GPU.  Every meshd has it beside its GPU
 * backend, and a program whose manifest lists "wasm" first (or lists no
 * backend this meshd's GPU runs) runs there, on lease lanes -- threads that
 * play mesh-rt's node loop against the same mailbox words, each lane running
 * its tickets in its own wasm instance (the guest runtime is crates/mesh-rt-wasm).
 * `meshd --backend wasm` is a CPU-only meshd: H_BACKEND reads BACKEND_WASM. */
#define MESH_BACKEND_WASM 2u
/* Native worker processes (mesh-host `native.rs`): a manifest's
 * `backends = ["native"]` (alone) makes the program an executable built per
 * target that runs as its own process beside the GPU backend (see "native
 * workers" below).  An executor, never an H_BACKEND kind. */
#define MESH_BACKEND_NATIVE 3u
#define MESH_BACKENDS_LEN 4
extern const char *const MESH_BACKENDS[4];

/* The H_BACKEND / M_BACKEND word: backend and family. */
static inline uint32_t mesh_backend_word(uint32_t kind, uint32_t family) {
    return (kind & 0xffu) | (family & 0xffu) << 8;
}
static inline uint32_t mesh_backend_kind(uint32_t word) { return word & 0xffu; }
static inline uint32_t mesh_backend_family(uint32_t word) { return (word >> 8) & 0xffu; }

#endif
