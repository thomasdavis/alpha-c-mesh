# @ajax/tui-sparkline

A one-row-per-eighth bar sparkline of u64 values, as ratatui's Sparkline.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows ratatui-widgets 0.3.2 `sparkline.rs`.

## Tests

`tests/sparkline.c`, and @ajax/tui-golden's sparkline cases.

## Licence

MIT. See the repository's LICENSE.
