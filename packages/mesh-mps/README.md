# @ajax/mesh-mps

meshd's CUDA backend, both isolation modes: blink (every program a resident kernel of meshd's one context: load, shape, scratch, launch, query, unload) and `--isolation mps` (a private `nvidia-cuda-mps-control` daemon under `--mps-dir`, mode 0700, quit at exit; stale daemons of dead meshds reaped; a pool of idle worker processes started before any program runs; each image preloaded into its own MPS client on the JIT thread; every worker checked to be a client of our server; back to the pool after an unload). The compiler checks the manifest, the PTX hash, the node's block limits and register cap, and links with nvJitLink. Also the native GPU workers' server check and pipe dir.

## Differences

- Worker processes run in a process group of their own (a terminal's ^C reaches meshd, which stops them at exit), as native workers do.
- An unknown CUresult prints as `cudaError_enum(N)`.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/mps.rs (`Gpu`, `Kernel`, `Stream`, `CudaBackend`, `CudaCompiler`, the daemon, `Mps`, `Proc`, `Worker`, the pool, `cleanup`, `native_pipe_dir`, `check_native`).

## Tests

`tests/mps.c`, without a GPU: blink mode over @ajax/cuda-driver-fake (a launched kernel plays the program's node loop on the real mailbox: 100 tickets, a halt, the shape and occupancy facts, compiler refusals with their messages) and `--isolation mps` for real: the test binary is also a fake `nvidia-cuda-mps-control` (on PATH) and the `meshd worker` processes (on the fake device), so the daemon starts, the pool's worker comes up and is checked as a client, two programs run through it one after the other (the second reuses the pooled worker), and the cleanup kills the workers and quits the daemon.

## Licence

MIT. See the repository's LICENSE.
