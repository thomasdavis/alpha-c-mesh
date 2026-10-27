# @ajax/mesh-peer-codec

The fleet's peer link frames, byte for byte as the Rust writes them (mesh-abi "peer frames v6"):
`F_JOIN` with this machine's capabilities and the checks that refuse a link (version, cluster size,
who dials whom), the election and commit messages of `@ajax/mesh-fleet-core` (`F_VOTE_REQ`, `F_VOTE`,
`F_STATE`, `F_STATE_ACK`, `F_COMMIT`), `F_BEAT` (the core's beat plus the member's reconcile status,
reason and move outcomes) and `F_HELLO` (a lockstep group member ready for an epoch and cycle).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/peer.rs`: `Caps`,
`Join` (`encode`, `decode`, `check`), `BeatStatus`, `msg_frame`, `beat_frame`, `beat_decode`,
`moves_bytes`, `moves_decode`, `hello_frame`, `hello_decode`, the reader's fixed-size decodes; and
`shared.rs` `HelloSpec`.

## Tests

`tests/codec.c`: the Rust's `join_frame_and_checks` and `fleet_frames_round_trip`.

## Licence

MIT. See the repository's LICENSE.
