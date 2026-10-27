# @ajax/mesh-native-limits

A native worker's limits from its manifest (`cpu`, `mem_mb`, `nice`, `background`): their LIMIT_* bits, whether this machine can enforce the CPU and memory caps (`systemd-run --user --scope` probed once), and the check that a spawned process's cgroup v2 really has them (`cpu.max`, `memory.max`, `memory.swap.max`). macOS refuses CPU and memory caps.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/native.rs (`Limits`, `limits_available`, `check_cgroup`).

## Tests

`tests/limits.c`: the Rust's `limit_bits_and_macos_refusals`, the probe on this machine, and the cgroup check of a process outside any scope.

## Licence

MIT. See the repository's LICENSE.
