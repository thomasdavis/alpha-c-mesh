# @ajax/mesh-backend

The executor interface meshd's driver sees: `mesh_backend`, `mesh_compiler`, `mesh_kernel` (a loaded image), `mesh_stream` (where a slot's work runs) and `mesh_prepared` (an image compiled off the driver thread). The Rust's traits are tables of functions (`ops` + `self`); the trait's default methods apply when an op is NULL. Also the free functions of backend.rs: `mesh_pick_exec`, `mesh_runs_here`, `mesh_prepare_any`, `mesh_check_manifest`, the backend label, the exit cleanups (native first, then MPS), and a native program's binary path for this target.

Implemented by @ajax/mesh-mps (CUDA, blink and MPS), @ajax/mesh-native (worker processes) and the wasm executor.

## Differences

- Errors are `char **err` (the Rust's `Result<_, String>` messages).
- `Kernel` and `Stream` are open (an executor provides its ops) instead of a closed enum.
- Exit cleanups are registered by the executors (`mesh_backend_on_cleanup`) instead of being called by name.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/backend.rs.

## Tests

`tests/backend.c`: labels, `pick_exec` and `check_manifest` refusals with the Rust's messages, `prepare_any` over fake compilers, and the cleanup order.

## Licence

MIT. See the repository's LICENSE.
