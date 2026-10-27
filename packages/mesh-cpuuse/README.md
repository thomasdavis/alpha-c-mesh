# @ajax/mesh-cpuuse

What the CPU executors cost per slot (CPU ns, resident bytes, lanes or processes), the machine's native-worker counters, and the per-thread meter a wasm lane keeps (`mesh_meter_start` / `tick` / `end`).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/cpuuse.rs.

## Tests

`tests/cpuuse.c`: the Rust's `meter_counts_this_threads_cpu_and_memory`.

## Licence

MIT. See the repository's LICENSE.
