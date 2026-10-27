# @ajax/tui-render

Drawing buffers to a terminal: the cells that changed since the last frame, as ANSI escapes, as ratatui's Terminal with crossterm.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows ratatui's Terminal and BufferDiff with crossterm 0.29's escapes.

## Tests

`tests/render.c`: the escapes for diffs, wide cells and colours.

## Licence

MIT. See the repository's LICENSE.
