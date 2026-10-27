# @ajax/mesh-top-ui-infer

mesh-top's Infer tab: every machine's model curve against the target samples, and the params infer's last run read.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/ui.rs` ("5 infer").

## Tests

@ajax/mesh-top-tests' golden replay draws it in 167 frames byte-identical to the Rust's (text and styles); @ajax/mesh-top-tests ports the Rust assertions on it.

## Licence

MIT. See the repository's LICENSE.
