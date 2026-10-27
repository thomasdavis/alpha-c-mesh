# @ajax/mesh-jobd-store

The job log file, an append-only file of job log records (`@ajax/mesh-jobs-log`).

- **Appends.** Records are fsynced before anything that depends on them leaves (`mj_durable`). A
  write or sync that fails is fatal to the caller (fail-stop). A write that failed half-way is first
  cut back to the last whole record, so a torn tail is never followed by a record (review finding 4).
- **Start.** The log is replayed, a torn last record is dropped, and the file is rewritten compacted.
- **Compaction.** When the file grows past twice its last compact size plus 8 MiB, it is rewritten
  whole as the replica's own log, through a renamed file, so a failed compaction leaves the log as it
  was.
- **In memory.** Without a path, nothing is written.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobd.rs`: `Store` (`open`, `rewrite`, `wants_compact`, its `Disk`).

## Tests

`tests/store.c`: the Rust's `a_torn_write_is_cut_back` (a failed half-write cut back, a torn tail at
start dropped and rewritten), compaction, and the in-memory store.

## Licence

MIT. See the repository's LICENSE.
