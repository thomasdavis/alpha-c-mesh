# @ajax/mesh-worker

The SDK of native worker programs: `mesh_worker_run` checks the native ABI, maps meshd's mailbox from its descriptor, says `ready`, runs the slot's nodes at each `run <gen> <slot>`, says `halted <gen>`, and exits on SHUTDOWN_EXIT, `exit`, or EOF (meshd gone). `mesh_worker_panic` is Rust's panic: a message and status 101.

## Differences

- Rust's panic hook catches every panic; in C a program reports its own bugs with `mesh_worker_panic` (a signal is reported by meshd as a signal).
- Commands are read with `read(2)`, not stdio, so a node that calls `exit()` never waits on a reader blocked in stdio.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-worker/src/lib.rs (`run`, `map_mailbox`, the command thread).

## Tests

Through @ajax/mesh-native's lifecycle tests, which run apps/mesh-test-worker (built on this SDK) under the native executor.

## Licence

MIT. See the repository's LICENSE.
