# @ajax/mesh-top-ui-train

mesh-top's Train tab: per machine the loss curve on a log scale, step, rate, params, peer and slowest wait, and whether the lockstep params are bitwise identical across machines.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/ui.rs` ("4 train").

## Tests

@ajax/mesh-top-tests' golden replay draws it in 167 frames byte-identical to the Rust's (text and styles); @ajax/mesh-top-tests ports the Rust assertions on it.

## Licence

MIT. See the repository's LICENSE.
