# @ajax/mesh-fleet-store

A fleet member's state file (`--fleet-state`): its term, vote and documents (`@ajax/mesh-fleet-persist`)
written whole to a temp file, `fsync`ed, renamed into place and the directory `fsync`ed, so nothing a
vote, acknowledgement or commit depends on is lost to a crash; `mfs_open` reads it (an absent file is a
fresh member; one written for another cluster size is refused) and writes it back at once.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/peer.rs`: `Store`.

## Tests

`tests/store.c`: the Rust's `store_persists`.

## Licence

MIT. See the repository's LICENSE.
