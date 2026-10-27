# @ajax/tui-canvas

A drawing surface of braille dots, half blocks or characters: points and clipped lines in world coordinates, as ratatui's Canvas.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows ratatui-widgets 0.3.2 `canvas.rs` and its grids (braille, half block, character), lines (Cohen-Sutherland clipping), points, labels.

## Tests

`tests/canvas.c`, and @ajax/tui-golden's chart cases.

## Licence

MIT. See the repository's LICENSE.
