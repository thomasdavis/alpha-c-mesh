# @ajax/mesh-top-snap

mesh-top's view of a meshd snapshot: framing checks, sections, the host block (members, fleet, jobs, load, network), trace records.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/snap.rs`.

## Tests

`tests/snap.c`: tests/snap.rs ported (framing, sections, fields, the host block, the trace reply); the mock round trip is in @ajax/mesh-top-mock.

## Licence

MIT. See the repository's LICENSE.
