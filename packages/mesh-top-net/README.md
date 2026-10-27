# @ajax/mesh-top-net

Network rates from meshd's cumulative counters on meshd's own clock (links, NICs, ports), and their human formatting.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/net.rs`.

## Tests

`tests/net.c`: its unit tests; @ajax/mesh-top-tests' net.c ports tests/net.rs.

## Licence

MIT. See the repository's LICENSE.
