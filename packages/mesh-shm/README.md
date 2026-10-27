# @ajax/mesh-shm

The mailbox's memory: a zeroed MAILBOX_BYTES shared-memory file (a close-on-exec memfd on Linux; an unlinked `shm_open` on macOS) mapped `MAP_SHARED`, which native workers and MPS workers map too (`/proc/<pid>/fd/<fd>`).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/shm.rs.

## Tests

`tests/shm.c`: the Rust's `two_mappings_share_words` and `shared_with_a_child_process`, the `/proc` path, and a full-size mailbox.

## Licence

MIT. See the repository's LICENSE.
