# @ajax/mesh-place-spec

A placeable program's regions, as its manifest declares them (`mps_spec`): the `place` capability, the
shared state regions, the frame number's offset, the output regions (part b of a frame is the b-th of
`fanout` equal pieces of each) and the show region; and their arithmetic (`mps_state_bytes`,
`mps_out_bytes`, `mps_part`, `mps_part_in_buffer`). The manifest parser (`@ajax/mesh-manifest*`) fills
one; `@ajax/mesh-placer` moves exactly these bytes between machines.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/manifest.rs`:
`Place` and its methods (the data placement needs; parsing it from `program.toml` stays with the
manifest).

## Tests

`tests/spec.c`.

## Licence

MIT. See the repository's LICENSE.
