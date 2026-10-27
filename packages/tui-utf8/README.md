# @ajax/tui-utf8

UTF-8 decoding, encoding and repair exactly as Rust's String::from_utf8_lossy (one U+FFFD per maximal ill-formed subpart).

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

See below.

## Tests

See `tests/`.
