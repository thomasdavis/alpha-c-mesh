# @ajax/tui-term

The terminal itself: raw mode, the alternate screen, size, resize signals and timed reads, with termios.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows crossterm 0.29's raw mode, alternate screen, cursor and size handling on Unix.

## Tests

`tests/term.c` (what runs without a terminal).

## Licence

MIT. See the repository's LICENSE.
