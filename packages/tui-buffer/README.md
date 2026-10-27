# @ajax/tui-buffer

A grid of styled terminal cells, one grapheme each, as ratatui's Buffer and Cell.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows ratatui-core 0.1 `buffer/buffer.rs` and `buffer/cell.rs`.

## Tests

`tests/buffer.c`: set_stringn with wide graphemes, styles, clear.

## Licence

MIT. See the repository's LICENSE.
