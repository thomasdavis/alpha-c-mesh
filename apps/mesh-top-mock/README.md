# @ajax/mesh-top-mock-meshd

A mock meshd control port for trying mesh-top without a GPU: synthetic snapshots, trace and log (a viewer fixture only).

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `examples/mock_meshd.rs`.

## Tests

The mock itself is tested in @ajax/mesh-top-mock.

## Licence

MIT. See the repository's LICENSE.
