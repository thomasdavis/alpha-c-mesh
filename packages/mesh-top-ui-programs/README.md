# @ajax/mesh-top-ui-programs

mesh-top's Programs tab: a program table per machine (kind, state, placement, rates, task-time quantiles, executor CPU and RSS, PTX hash), the selected program's view pane with a hexdump, the trace flame chart and the log.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/ui.rs` ("2 programs").

## Tests

@ajax/mesh-top-tests' golden replay draws it in 167 frames byte-identical to the Rust's (text and styles); @ajax/mesh-top-tests ports the Rust assertions on it.

## Licence

MIT. See the repository's LICENSE.
