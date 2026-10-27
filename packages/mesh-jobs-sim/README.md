# @ajax/mesh-jobs-sim (private)

The test bed for the job nodes. It is not published.

- `tests/trace.c`: the job nodes against the Rust, output for output. A deterministic fleet runs
  under a scripted view: the lowest-id member up leads while a majority is up, and a new leader is a
  new term. Four scenarios cover one meshd, three members, five members with payloads and dropped
  frames, and the fixed pair with a failing disk. The harness is identical to the Rust's
  (`tests/gold_trace.rs` in the golden-vector crate). Every output of every node is hashed, frames
  and log records by their wire bytes. Every 500 ms the hashes, output counts, committed tables and
  pending entries must equal the Rust's.

## Licence

MIT. See the repository's LICENSE.
