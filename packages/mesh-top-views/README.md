# @ajax/mesh-top-views

meshd's fleet, jobs and pipes views (one record a line: kind key=value .. -- text), parsed as mesh-top reads them.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top `src/views.rs`.

## Tests

`tests/views.c`: its unit tests; @ajax/mesh-top-tests' views.c ports tests/views.rs.

## Licence

MIT. See the repository's LICENSE.
