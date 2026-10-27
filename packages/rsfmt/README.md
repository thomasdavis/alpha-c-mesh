# @ajax/rsfmt

Numbers and padding formatted exactly as Rust's format! does: {:.N}, {:+.N}, {:.Ne}, NaN/inf, and width by characters.

Part of the C port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh)'s `mesh-top`, a
terminal UI built with ratatui: these packages reproduce what ratatui and its Unicode crates compute,
so a frame drawn in C is the same, cell for cell, as the Rust one.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): Rust's `core::fmt` for floats: `{:.N}`, `{:.Ne}`, widths, `+`, zero padding (exact decimal expansion, round half to even, NaN, inf).

## Tests

`tests/rust.c`: values formatted by Rust 1.88 (`formats.txt` from the scratch generator).

## Licence

MIT. See the repository's LICENSE.
