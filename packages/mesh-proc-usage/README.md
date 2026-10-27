# @ajax/mesh-proc-usage

A process's CPU time (user + system, every thread) and resident bytes, from `/proc/<pid>/stat`.

## Differences

Linux only (the Rust also reads macOS's `proc_pidinfo`).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/native.rs (`usage`).

## Tests

`tests/usage.c`: the Rust's `usage_of_self`.

## Licence

MIT. See the repository's LICENSE.
