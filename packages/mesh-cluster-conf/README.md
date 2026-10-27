# @ajax/mesh-cluster-conf

A fleet's cluster config: one `key = value` file describing every member (`systems = N`, each
`k.peer = host:port`, an optional `k.listen`), the liveness timing (`ping_ms`, `dead_ms`) and the
scheduler's `sched_*` policy. `mcc_topology_parse` gives one member's view: where it listens (systems
k > 0) and whom it dials (every j > k).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/cluster.rs`
(`Topology`, `Tunables`, `keys`, `topology`, `pair_dialer`, `pair_listener`).

## Tests

`tests/cluster.c`: the Rust's `three_systems` and `errors` tests, with the exact error messages.

## Licence

MIT. See the repository's LICENSE.
