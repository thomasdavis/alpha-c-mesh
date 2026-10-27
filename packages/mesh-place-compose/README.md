# @ajax/mesh-place-compose

Moving a placed program's bytes: its state regions read and written through the view
(`mpc_read_state`, `mpc_write_state`), its frame parts gathered from the output regions and scattered
into a show buffer (`mpc_gather`, `mpc_scatter`, `mpc_sys_nibbles`), and the Composer (`mpc_add`), which
matches the parts of a frame arriving from several machines and yields the frame only once it has every
part (older incomplete frames are dropped; at most 8 kept).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/place.rs`:
`read_state`, `write_state`, `gather`, `scatter`, `sys_nibbles`, `Composer`, `Whole`, `PENDING`.

## Tests

`tests/compose.c`: the Rust's `gather_scatter_state` and `composer_matches_frames`.

## Licence

MIT. See the repository's LICENSE.
