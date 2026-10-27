# @ajax/mesh-assemble-bmp

A 24-bit BMP of an RGB raster: rows bottom-up, BGR, each row padded to 4 bytes, 2835 pixels per
metre. What `render <job> bmp` answers with.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/assemble.rs`: `bmp`.

## Tests

`tests/bmp.c`: the Rust's check (in `default_reference_digests`), the header fields, and two rows
written bottom-up.

## Licence

MIT. See the repository's LICENSE.
