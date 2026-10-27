# @ajax/rust-fmt

Text exactly as Rust's `format!` writes it, where C's `printf` differs:

- `rf_f32` / `rf_f64`: a float's Display (`{}`): the shortest digits that read back as the same value,
  never an exponent, no trailing `.0` (`1.0` is `1`, `1e-7` is `0.0000001`, NaN is `NaN`).
- `rf_debug_str`: a string's Debug (`{:?}`): quoted, with `\n`, `\t`, `\r`, `\\`, `\"`, `\0` and
  `\u{..}` escapes.
- `rf_hex_alt`: `{:#x}` (`0x0` for zero, where C's `%#x` prints `0`).

Everything else in Rust's formatting maps onto printf: `{:.2}` is `%.2f` (both round the exact binary
value to nearest, ties to even), `{:016x}` is `%016llx`, `{:+.2}` is `%+.2f`.

## Ported from

The formatting the [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) crates rely on
(`core::fmt`'s float Display and `str::escape_debug`), for the fleet's wire formats and messages
(`fleet.rs`, `sched.rs`), which must match the Rust byte for byte.

## Tests

`tests/fmt.c`: outputs checked against rustc, and a sweep showing every f32 Display reads back as itself.

## Differences

`rf_debug_str` escapes control bytes only; Rust also escapes some non-printable Unicode (e.g. lone
combining marks at the start). Nothing in the fleet's messages contains them.

## Licence

MIT. See the repository's LICENSE.
