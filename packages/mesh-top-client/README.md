# @ajax/mesh-top-client

mesh-top's endpoint threads: one persistent meshd control-port connection each, polling snap and running the viewer's commands, with backoff and per-command reply timeouts.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/client.rs`.

## Tests

@ajax/mesh-top-tests' client.c: tests/client.rs ported (polling, commands, reconnect, offline backoff) against the mock over TCP.

## Licence

MIT. See the repository's LICENSE.
