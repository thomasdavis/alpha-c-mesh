# @ajax/tui-arena

A bump allocator for one frame of a terminal UI: allocate freely, free everything at once.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows ratatui allocates per frame with Rust's ownership; this is the C port's equivalent (no Rust counterpart).

## Tests

`tests/arena.c`: allocation, growth, formatting, reset.

## Licence

MIT. See the repository's LICENSE.
