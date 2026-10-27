# @ajax/mesh-jobs-wire

The binary encoding the job table's ops, the job frames (F_JOB_*, F_LEASE, F_RESULT) and the job log
are written in: little-endian `u8`, `u32`, `u64`; bytes as a `u32` length and the bytes; strings as
their UTF-8 bytes the same way; item ranges as a `u32` count and `(start, end)` pairs. The reader
keeps its first failure with jobs.rs's exact message (`job frame: short (3 of 8 bytes at 17)`,
`job frame: not UTF-8`), and every read after it fails.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`, the `W` writer and the `R` reader.

## Differences

A string read from the wire becomes a NUL-terminated C string, so one holding a NUL byte (valid UTF-8,
legal in a Rust `String`) is cut at it. Nothing in the mesh writes such a name.

## Tests

`tests/wire.c`: the exact bytes, reads back, the short-read message and its stickiness, and the UTF-8
checks (overlong, surrogate, truncated, past U+10FFFF).

## Licence

MIT. See the repository's LICENSE.
