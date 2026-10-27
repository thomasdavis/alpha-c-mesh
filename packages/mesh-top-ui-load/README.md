# @ajax/mesh-top-ui-load

mesh-top's load panes: the Load tab (GPU share, host CPU and pressure, cores, per-program task time, share history), a machine panel's Load box and the overview's load cells.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/ui.rs` ("8 load").

## Tests

@ajax/mesh-top-tests' golden replay draws it in 167 frames byte-identical to the Rust's (text and styles); @ajax/mesh-top-tests ports the Rust assertions on it.

## Licence

MIT. See the repository's LICENSE.
