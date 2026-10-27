# @ajax/mesh-parse

Numbers parsed exactly as Rust's `str::parse` (and `from_str_radix(16)`) accepts them: an optional `+`
(or `-` for signed), digits only, no whitespace, no overflow; Rust's float grammar (`.5`, `5.`, `1e3`,
`inf`, `NaN`, no hex floats). C's `strtoul` would accept `" -1"` and wrap it.

## Ported from

`str::parse::<u64 | u32 | u16 | i32 | f32 | f64>()` and `u64::from_str_radix(_, 16)`, as the
[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) fleet uses them to parse its documents,
reports and cluster configs.

## Tests

`tests/parse.c`: what Rust accepts and refuses.

## Licence

MIT. See the repository's LICENSE.
