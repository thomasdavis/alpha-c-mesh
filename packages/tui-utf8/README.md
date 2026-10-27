# @ajax/tui-utf8

UTF-8 decoding, encoding and repair exactly as Rust's String::from_utf8_lossy (one U+FFFD per maximal ill-formed subpart).

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` depends on it through ratatui: Rust's `String::from_utf8_lossy` and `char_indices`.

## Tests

`tests/utf8.c`.

## Licence

MIT. See the repository's LICENSE.
