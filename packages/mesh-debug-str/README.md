# @ajax/mesh-debug-str

A string quoted the way Rust's `{:?}` prints a `&str`: in double quotes, with `\"`, `\\`, `\t`, `\r`,
`\n`, `\0` and `\u{..}` for other control characters. The port's error messages use it so they read
exactly as the Rust's (`items "x": ...`).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): the many
`{s:?}` in `crates/mesh-host`'s messages (`jobs.rs` among them).

## Differences

Rust also escapes Unicode grapheme extenders and unprintable non-ASCII characters (`\u{..}`); here
non-ASCII UTF-8 passes through unchanged. The mesh's names and words are ASCII.

## Tests

`tests/debug_str.c`: every escape, against what `format!("{:?}", s)` prints.

## Licence

MIT. See the repository's LICENSE.
