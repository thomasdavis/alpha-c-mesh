# @ajax/mesh-worker-ctx

What a native worker's `init` and `task` see: mesh-rt's Ctx on a CPU thread. `mesh_view` windows record P_FAULT on any out-of-range or misaligned access (reads give 0, writes are dropped), as on the GPU. The Ctx gives ids, the own view, peer views (the `reads` mask re-read at each call), program lookup, the input queue (concurrent pops by CAS), pipes (push with backpressure, pop, length), the request window and its response, counters, and run requests for READS_ALL programs.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-worker/src/lib.rs (`Mb`, `View`, `Ctx`, `entry`).

## Tests

Through @ajax/mesh-worker-node's tests (the Rust's three unit tests).

## Licence

MIT. See the repository's LICENSE.
