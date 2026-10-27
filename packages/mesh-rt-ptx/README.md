# @ajax/mesh-rt-ptx

mesh-rt's device primitives on NVIDIA GPUs, as inline PTX in C (`mesh/rt/ptx.h`): special registers
(`%tid`, `%ctaid`, `%ntid`, `%smid`, `%globaltimer`, `%dynamic_smem_size`), system-scope loads and stores
(acquire, release, relaxed), atomics (`atom.add/or/and/cas`, `red.add`), fences, `nanosleep`, warp
shuffles and ballot, the warp and block barriers, the node's dynamic shared memory (`mesh_smem`), and
`trap`. Every asm has a `"memory"` clobber, as Rust's `asm!` without `nomem` is a compiler barrier.

The header is empty unless compiled for nvptx64 (`clang --target=nvptx64-nvidia-cuda`). Programs include
`mesh/rt/prim.h` (@ajax/mesh-rt-prim), which picks this or the CPU simulator.

- **Ported from:** alpha-rust-mesh `crates/mesh-rt/src/ptx.rs`, instruction for instruction.
- **Tests:** every program under `programs/` compiles these into PTX in each test run; @ajax/mesh-prog-hello's
  linked PTX is checked for its entry, `MESH_RT_ABI` and the float mode.
- **Differences:** `rdf`/`wrf`, `shfl_idx_f` and `warp_sum` (target-neutral) live in @ajax/mesh-rt-prim.
