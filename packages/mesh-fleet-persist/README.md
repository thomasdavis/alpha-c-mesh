# @ajax/mesh-fleet-persist

What a fleet member keeps on disk (`mfp_persist`): its term, its vote in that term, the last document
it accepted from a leader and the last it knows committed. `mfp_encode` / `mfp_decode` are the Rust's
state file text (`systems N`, `term`, `voted`, then each document prefixed by its length); a file
written for another cluster size is refused. Writing it safely (temp, fsync, rename, directory fsync)
is `@ajax/mesh-fleet-store`.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/fleet.rs`:
`Persist` (`encode`, `decode`).

## Tests

`tests/persist.c`: the Persist part of the Rust's `document_round_trips`, and each error.

## Licence

MIT. See the repository's LICENSE.
