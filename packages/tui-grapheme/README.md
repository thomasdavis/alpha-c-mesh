# @ajax/tui-grapheme

Extended grapheme clusters (UAX #29, Unicode 17), the same boundaries as Rust's unicode-segmentation 1.13.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT)'s `mesh-top` depends on it through ratatui: unicode-segmentation 1.13.3 (extended grapheme clusters, UAX #29 with the InCB conjunct rule), tables generated from its source.

## Tests

`tests/graphemes.c`: the boundaries of `tests/data/mixed.txt` (1768 clusters) hash as the Rust's.

## Licence

MIT. See the repository's LICENSE.
