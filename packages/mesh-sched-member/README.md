# @ajax/mesh-sched-member

One fleet member as the scheduler sees it (`msm_member`): its capabilities from F_JOIN, its CPUs,
RAM, view heap, link RTTs and failures from F_PERF, and its live load from F_LOADREP; and a program's
measured performance on a member (`msm_perf`), in a map sorted by (program, member) as the Rust's
`BTreeMap<(String, u32), Perf>` is.

`mof32` is Rust's `Option<f32>`. Host, executor and architecture are fixed-size strings (64, 32 and
32 bytes): the wire gives at most 32, 32 and 16.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/sched.rs`:
`Member` (`executors`, `exec_for`, `rtt_to`) and `Perf`.

## Tests

`tests/member.c`; the scheduler's tests (`@ajax/mesh-sched-place`) exercise it throughout.

## Licence

MIT. See the repository's LICENSE.
