# @ajax/tui-unicode-width

Terminal column widths of characters and strings, exactly as Rust's unicode-width 0.2.2 (Unicode 17).

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` depends on it through ratatui: unicode-width 0.2.2, tables generated from its source.

## Tests

`tests/width.c`: the width of every code point hashes as the Rust's.

## Licence

MIT. See the repository's LICENSE.
