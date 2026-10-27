# @ajax/tui-chart

Line and scatter charts with axes, labels and a legend, as ratatui's Chart.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` uses it through ratatui; the C follows ratatui-widgets 0.3.2 `chart.rs` (Chart, Axis, Dataset, the legend).

## Tests

`tests/chart.c`, and @ajax/tui-golden's chart cases.

## Licence

MIT. See the repository's LICENSE.
