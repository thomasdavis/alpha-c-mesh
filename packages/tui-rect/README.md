# @ajax/tui-rect

Rectangles of terminal cells with saturating u16 arithmetic, as ratatui's Rect.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows ratatui-core 0.1 `layout/rect.rs`.

## Tests

`tests/rect.c`.

## Licence

MIT. See the repository's LICENSE.
