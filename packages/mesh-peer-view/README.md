# @ajax/mesh-peer-view

Two pieces of the fleet thread's scheduler glue: a migrated view arriving as F_VIEW chunks from its
source (`mpv_take`: the total must fit the program's view and stay the same across chunks, no chunk may
pass it, and a duplicate chunk counts once), and a member's load smoothed over its once-a-second
samples (`mpv_load_add`, a time constant of about 5 samples) so a tenant's spike does not look busy.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/peer_sched.rs`:
`ViewWait`, `view_take`, `LoadAvg`, `LOAD_ALPHA`.

## Tests

`tests/view.c`: the Rust's `view_chunks_are_bounded_and_counted_once` (review finding 11), and the
smoothing.

## Licence

MIT. See the repository's LICENSE.
