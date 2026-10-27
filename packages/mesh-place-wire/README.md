# @ajax/mesh-place-wire

The placement peer frames, byte for byte as the Rust writes them: `F_PLACE` (a placement change: op,
id, name, mode with its assignment, aux, then a state or a reason), `F_FRAME` (one frame's state and the
parts its sender rendered), `F_INPUT` (input for a program's owner), and the 16-byte wire name. Each
`*_rest_len` checks the body's declared length against its maximum before anything is read.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/place.rs`:
`name16`, `name_of`, `PlaceMsg`, `FrameMsg`, `encode_input`, `input_len`, `PLACE_HEAD`, `FRAME_HEAD`,
`INPUT_HEAD`, `AUX_AUTO`.

## Tests

`tests/wire.c`: the Rust's `frames_round_trip`, and the name checks of peer.rs `names_on_the_wire`.

## Licence

MIT. See the repository's LICENSE.
