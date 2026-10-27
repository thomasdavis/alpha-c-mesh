# @ajax/tui-solver

The Cassowary incremental simplex solver for linear constraints with strengths, ported from kasuari 0.4 (ratatui's).

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows kasuari 0.4 (the Cassowary simplex solver ratatui uses): symbols, rows, the dual optimise, edit-free constraints, strengths.

## Tests

`tests/solver.c`; @ajax/tui-layout's 3000 random layouts exercise it end to end.

## Licence

MIT. See the repository's LICENSE.
