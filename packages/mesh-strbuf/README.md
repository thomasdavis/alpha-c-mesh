# @ajax/mesh-strbuf

A growable buffer of bytes, always NUL-terminated so it is text too, with `printf`; `printf` into a new
string (`mfmt`); `mstrdup`; and allocation that aborts instead of returning NULL. The jobs and blobs
packages build their wire frames, logs and control answers with it, as the Rust builds `Vec<u8>` and
`String`.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): the C port's
replacement for Rust's `String`, `Vec<u8>` and `format!`.

## Tests

`tests/strbuf.c`: appends, growth past a thousand `printf`s, embedded NULs, `msb_take`, `mfmt`.

## Licence

MIT. See the repository's LICENSE.
