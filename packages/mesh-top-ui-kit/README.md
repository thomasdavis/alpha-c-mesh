# @ajax/mesh-top-ui-kit

What every mesh-top pane shares: the dark palette, label and value spans, the rounded panel, machine titles, Rust-exact number formats and the no-snapshot fallbacks.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/ui.rs` (the palette and helpers).

## Tests

`tests/kit.c`; @ajax/mesh-top-tests' golden replay draws it in 167 frames byte-identical to the Rust's (text and styles).

## Licence

MIT. See the repository's LICENSE.
