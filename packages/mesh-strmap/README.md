# @ajax/mesh-strmap

A string-to-string map kept sorted by key, bytewise (Rust's `BTreeMap<String, String>` order), for
config keys and anything else the fleet iterates in Rust's order.

## Ported from

`BTreeMap<String, String>` as [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s
`cluster.rs` (`keys`) and `sched.rs` (`Weights::from_keys`) use it.

## Tests

`tests/strmap.c`.

## Licence

MIT. See the repository's LICENSE.
