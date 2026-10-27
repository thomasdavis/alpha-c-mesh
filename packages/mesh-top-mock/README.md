# @ajax/mesh-top-mock

A tiny mock of meshd's control port for mesh-top's tests and demo: synthesised snapshots, trace and log, a fake catalog, and a threaded TCP server.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `tests/support/mock.rs`.

## Tests

`tests/mock.c`: tests/snap.rs's mock round trip, and the control port spoken raw.

## Licence

MIT. See the repository's LICENSE.
