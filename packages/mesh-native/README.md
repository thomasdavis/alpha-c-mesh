# @ajax/mesh-native

The native executor: programs that are executables (`backends = ["native"]`) run as worker processes on the shared-memory mailbox. The compiler checks the binary for this target (an ELF of this machine, executable), the identity (manifest + `source.hash` + param), the limits and GPU access; the image keeps at most one process: launch sends `run` or spawns off the driver thread, a halt keeps the process, an exit reaps it (killed after the grace), a death fails the slot with the exit status and the output's tail; outputs go to `<logs>/<name>.log` and the log. The backend is isolated and not resident, owns its halts and re-reads `reads` live.

## Differences

- A worker started for a launch that was released before it came up is retired at once (the Rust leaves it to the exit cleanup).
- A missing `source.hash` says to build it with `mesh-build-programs` (the C port's tool) instead of the Rust's script.
- GPU workers under MPS are checked through a callback (`mesh_worker_cfg.check_gpu_client`, @ajax/mesh-mps's `mesh_mps_check_native`) so this package needs no CUDA.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/native.rs (`Proc`, `Host`, `Worker`, `NativeBackend`, `NativeCompiler`, `check_format`, `cleanup`).

## Tests

`tests/lifecycle.c`: crates/mesh-worker/tests/lifecycle.rs one for one, against apps/mesh-test-worker in a real shared mailbox: spawn and `ready`, tasks, a halt that keeps the process, a stop that reaps it, a new process after a stop, warm launches under 100 ms, crashes (a panic, `exit(3)`, SIGTERM) with their status and output tail, every refusal, identities, and the memory cap (an OOM kill) and CPU cap under systemd scopes.

## Licence

MIT. See the repository's LICENSE.
