# @ajax/mesh-rs-parse

Integers parsed exactly as Rust's `str::parse::<u32 | u64 | i32 | ...>()` and `uN::from_str_radix`
parse them: an optional `+` (or `-` for a signed type), then at least one digit and nothing else, and
a value that fits the type. The port reads the Rust's text formats (job specs, control requests) with
it, so what one accepts the other does.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): the C port's
equivalent of Rust's integer `FromStr`, used by `jobs.rs`'s text forms.

## Tests

`tests/rs_parse.c`: signs, bounds of u32, u64, i32 and i64, radix 16, and what Rust refuses (spaces,
`0x`, empty, a lone sign, overflow).

## Licence

MIT. See the repository's LICENSE.
