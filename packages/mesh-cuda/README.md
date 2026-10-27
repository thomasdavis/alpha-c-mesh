# @ajax/mesh-cuda

meshd's CUDA: `CUDA_MODULE_LOADING=EAGER` (refusing anything else), the per-machine libcuda override (`--cuda-lib` / `MESH_CUDA_LIB`, re-exec with `LD_LIBRARY_PATH`, checked in `/proc/self/maps`), device 0 with a primary or green context, the per-block limits and register caps of multi-warp nodes, the pinned mailbox (`cuMemHostRegister` DEVICEMAP), cubin modules with their launch shape, scratch memory zeroed on a stream of its own, module globals, occupancy, streams, launches, queries and unloads.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/cuda.rs.

## Tests

`tests/cuda.c`, against @ajax/cuda-driver-fake: eager-loading refusals, primary and green contexts, register caps, limit messages, a module's whole life (shape opt-in, zeroed scratch, `MESH_RT_ABI`, occupancy, a launch whose fake kernel writes the mailbox, query, unload) and the driver's error names.

## Licence

MIT. See the repository's LICENSE.
