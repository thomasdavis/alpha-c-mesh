# @ajax/mesh-rsfmt

The text Rust's formatting machinery produces, so the C meshd answers byte for byte as the Rust one:

| Rust | C |
|---|---|
| `{:?}` of a `str` (`"no program \"x\""`) | `mesh_rs_debug_str`, `mesh_rs_debug_cstr` |
| `{:?}` of a `Duration` (`10s`, `1.5s`, `500ms`, `40µs`) | `mesh_rs_duration`, `mesh_rs_duration_ms` |
| `{}` of an `f32` / `f64` (shortest round-trip digits, no exponent) | `mesh_rs_f32`, `mesh_rs_f64` |
| `{:.p}` of a float | `mesh_rs_fixed` |

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): the formats used in
`crates/mesh-host`'s replies and logs (core::fmt's `escape_debug`, core::time's `fmt_decimal`, the
float `Display` impl).

## Tests

- `tests/rsfmt.c`: every expected string is what rustc 1.88's `format!` prints for the same value
  (generated with a throwaway Rust program).

## Differences

- `{:?}` of a string escapes ASCII control characters exactly as Rust does; Rust also escapes some
  non-printable non-ASCII code points (C1 controls, unassigned and grapheme-extend characters), which
  this passes through unchanged. meshd's replies only quote names, paths and protocol words.

## Licence

MIT. See the repository's LICENSE.
