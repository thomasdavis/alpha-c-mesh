# @ajax/mesh-mps-wire

The frames between meshd and its `--isolation mps` worker processes: a u32 little-endian length (op + body), the op, its body; the W_* ops each way; frame reading with the 64 MiB bound; little-endian field helpers.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/mps.rs (`W_*`, `frame`, `read_frame`, `u32_at`).

## Tests

`tests/wire.c`: the Rust's `frames` test through a pipe, a zero-length frame, EOF, and the u32/u64 fields.

## Licence

MIT. See the repository's LICENSE.
