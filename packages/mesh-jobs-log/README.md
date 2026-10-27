# @ajax/mesh-jobs-log

What a member persists of the job table, and what that replays to. The log is append-only and has
four kinds of record:

- **LID** is the highest leadership the member followed or led. A restarted member never follows an
  older one, and leads above it.
- **SNAPSHOT** is the committed table at a commit point. It keeps the entries past that point
  (review finding 6).
- **ENTRY** is an entry not committed yet: a delta, or a replace (a new leader's merge, which drops
  older leaderships' entries).
- **COMMIT** marks that a leadership's entries up to n are committed.

`mj_replay` rebuilds the replica exactly. Entries never committed stay pending and never reach the
committed table (review finding 5). A torn last record ends the replay (review finding 4), while a
whole record that doesn't decode is an error. `mj_replica_log` writes the compacted log that replays
to the same replica.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `Log` (`encode`, `decode`), `Entry`, `Replica` (`apply`, `last`,
`log`), `replay`.

## Tests

`tests/log.c`:
- Four random sequences of 400 records, built from the same generator as the Rust golden test, replay
  to the same replica as in the Rust: its compacted log's hash, leadership, commit point, pending
  entries and `last`. Each compaction replays back to it.
- The log of the Rust's `ops_frames_and_the_merge`, byte for byte, with its torn tail dropped.
- Review finding 6's snapshot (`a_snapshot_keeps_the_entries_acknowledged_past_it`).
- Replay's answers to corrupt records, word for word the Rust's.

## Licence

MIT. See the repository's LICENSE.
