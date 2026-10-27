# @ajax/mesh-top-clock

Monotonic instants and durations with Rust's std::time arithmetic, and a virtual clock for tests.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, the terminal
dashboard for meshd clusters. Every frame it draws is checked against the Rust one, cell for cell
(text, colours and modifiers): see @ajax/mesh-top-tests.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-top's use of `std::time::Instant`, plus the virtual clock the golden scenarios run on (the scratch generator patches the Rust mock the same way).

## Tests

`tests/clock.c`.

## Licence

MIT. See the repository's LICENSE.
