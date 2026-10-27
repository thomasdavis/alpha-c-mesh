# @ajax/mesh-state-file

meshd's `--state` file: one `name dir 0|1` line per loaded program in slot order, `#` comments, written whole to `<path>.tmp` and renamed.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/loader.rs (`Saved`, `read_state`, `write_state`).

## Tests

`tests/state_file.c`: the state half of the Rust's `confined_and_catalog`, with the exact refusal message.

## Licence

MIT. See the repository's LICENSE.
