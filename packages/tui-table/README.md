# @ajax/tui-table

Rows of cells in columns laid out by constraints, with a header, selection and highlighting, as ratatui's Table.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows ratatui-widgets 0.3.2 `table/*.rs` (Table, Row, Cell, TableState, highlight spacing).

## Tests

`tests/table.c`, and @ajax/tui-golden's table cases.

## Licence

MIT. See the repository's LICENSE.
