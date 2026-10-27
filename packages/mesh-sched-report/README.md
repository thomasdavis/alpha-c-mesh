# @ajax/mesh-sched-report

A member's F_PERF report (`msr_report`): its capacity (CPUs, RAM, disk, the view heap's largest free
run and free slots), its programs' measured performance, the programs its reconciler failed, the link
RTTs it measured and the echoes that let the others measure theirs. `msr_encode` / `msr_decode` are
the Rust's line-oriented text, byte for byte.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/sched.rs`:
`Report` (`encode`, `decode`).

## Tests

`tests/report.c`: the Rust's `reports_round_trip`, the exact text, errors.

## Licence

MIT. See the repository's LICENSE.
