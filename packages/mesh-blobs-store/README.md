# @ajax/mesh-blobs-store

A member's blob store: job items' payloads, kept off the job log, content-addressed by their sha256
and **referenced per job**.

- **Layout.** A job's blob is the file `<dir>/<job>/<sha256 hex>`, either whole under its name or
  absent (`@ajax/mesh-blobs-durable` writes it). Every read checks the bytes against the name. A
  corrupt or missing file is removed, reads as absent, and is reported lost.
- **One blob, several jobs.** Two jobs that render the same scene compute byte-identical payloads.
  Each job holds its own reference, a hard link of the file another job's reference names, so the
  bytes are on disk once. Forgetting or evicting one job removes only its references. A blob goes with
  its last reference. (In the live fleet, forgetting job 1.2 once took job 2.1's committed blobs with
  it; this layout prevents that.)
- **Bounded.** At most `cap` bytes, each blob counted once. Room is made only by evicting *forgotten*
  jobs' references. A store full of listed jobs' blobs refuses: `job forget` a finished job to make
  room.
- **In memory.** Without a directory, blobs are kept in memory under the same cap.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/blobs.rs`: `Live`, `Make`, `Reserved`, `Store` (`open`, `reserve`, `written`,
`put_mem`, `get`, `take_lost`, `drop_job`, `jobs`, `all`, the counts).

## Differences

The Rust keeps its index in a `HashMap`, so `all()` and `drop_job()` list hashes in arbitrary order.
Here they come in hash order.

## Tests

`tests/store.c`: the Rust's four tests:
- `write_rename_durability`;
- `hash_verification`;
- `a_blob_of_two_jobs_outlives_either`, on disk and in memory, with the link race;
- `eviction`, on disk and in memory.

## Licence

MIT. See the repository's LICENSE.
