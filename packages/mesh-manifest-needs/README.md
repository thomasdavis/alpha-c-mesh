# @ajax/mesh-manifest-needs

What a program needs and prefers from the fleet scheduler: its manifest's `replicas`, `spread`, `prefer`, `avoid`, `arch`, `min_mem_mb`, `colocate_with`, `group*`, `priority`, `migrate` and the `cpu`/`mem_mb` reservations, as `mesh_needs`; placement terms (`cuda`, `host:NAME`, `system:K`, `arch:A`) with their parser.

`Term::matches` needs the scheduler's member type, so it lives with the scheduler.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/sched.rs (`Replicas`, `Migrate`, `Term`, `Needs`).

## Tests

Through @ajax/mesh-manifest-encode's golden test (the Needs' Debug output is compared with the Rust's).

## Licence

MIT. See the repository's LICENSE.
