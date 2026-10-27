# @ajax/tui-layout

Splitting a rectangle by constraints (Length, Min, Max, Percentage, Ratio, Fill) with flex and spacing, solved exactly as ratatui's Layout.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows ratatui-core 0.1 `layout/layout.rs`: constraints, flex, spacing, margins, the strengths and the order of every Cassowary constraint, and the 512-entry layout cache.

## Tests

`tests/basics.c`; `tests/random.c` replays 3000 random layouts from `tests/data/layouts.txt` (ratatui settles exact ties by HashMap order, which varies by process: the 31 layouts that vary across Rust runs list every result seen, two hand-checked as exact ties).

## Licence

MIT. See the repository's LICENSE.
