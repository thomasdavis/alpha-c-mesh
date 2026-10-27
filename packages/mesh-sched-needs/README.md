# @ajax/mesh-sched-needs

What a program needs and prefers from the fleet scheduler (`msn_needs`): backends, arch,
`min_mem_mb`, `prefer` / `avoid` terms (`cuda`, `host:dgx-00`, `system:2`, `arch:aarch64`),
`replicas = N | all`, spread or pack, `colocate_with`, the pipes it feeds, a lockstep group
(`group_size`, `group_members`), priority, `cpu` / `mem_mb` reservations, how it may migrate, and its
GPU node shape. `msn_encode` / `msn_decode` are the document's one-token form
(`b=cuda;ar=-;mm=0;...`), byte for byte the Rust's; `msn_summary` is the `sched` line.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/sched.rs`:
`Replicas`, `Migrate`, `Term` (`parse`, `word`, `matches`) and `Needs` (`default`, `encode`, `decode`,
`summary`).

## Tests

`tests/needs.c`: the Rust's `needs_round_trip_and_terms`, the exact encodings and summaries, every
decode error. The cpu value is written with Rust's float Display (`@ajax/rust-fmt`).

## Licence

MIT. See the repository's LICENSE.
