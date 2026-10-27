# @ajax/mesh-top

A terminal dashboard for meshd clusters: one poller per endpoint, every tab of crates/mesh-top (mesh, programs, doom, train, infer, http, net, load, fleet, jobs, pipes, render), and --print for one frame as text.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/main.rs`.

## Tests

The frame it prints is @ajax/mesh-top-ui's (tested there); `--print` against `mesh-top-mock` is the end-to-end check in docs/port/top.md.

## Licence

MIT. See the repository's LICENSE.
