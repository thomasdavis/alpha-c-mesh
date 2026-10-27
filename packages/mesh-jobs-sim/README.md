# @ajax/mesh-jobs-sim (private)

The test bed for the job nodes. It is not published.

- **`tests/trace.c` (the nodes against the Rust, output for output).** A deterministic fleet runs
  under a scripted view: the lowest-id member up leads while a majority is up, and a new leader is a
  new term. Seven scenarios cover one meshd, three members, five members with payloads and dropped
  frames, the fixed pair with failing disks, a hung member, heavy loss, and payload forgets. The
  harness is identical to the Rust's (`tests/gold_trace.rs` in the golden-vector crate). Every output
  of every node is hashed, frames and log records by their wire bytes. Every 500 ms the hashes, output
  counts, committed tables and pending entries must equal the Rust's.
- **`tests/fsim.h` (jobs.rs's simulated fleet, ported).** Members with the fleet's election core
  (`@ajax/mesh-fleet-core`), a job node, a fake executor, a log on a disk that can fail, an in-memory
  blob store that survives crashes, and FIFO links that can drop frames. Every 200 ms each member's
  committed results are checked: none changes, every holder named holds the job's own copy, and the
  log replays to the node's state. The replay is incremental: the records appended since the last
  check, applied to what the log replayed to before them.
- **The fleet tests**, all of jobs.rs's, split so each program runs in seconds to tens of seconds:
  - `fleet_basic.c`, `fleet_rates.c`, `fleet_fetch.c`;
  - `fleet_leader_<seed>.c`, `fleet_overwrite.c`;
  - `fleet_survive_<seed>.c`, `fleet_full_store_<n>.c`;
  - `fleet_assemble_<seed>.c`, the 24 seeds of `done_jobs_assemble_until_forgotten`;
  - `fleet_chaos_<n>.c`, the 40 chaos seeds in slices of five.

## Differences

- The Rust's chaos test asserts that crashes, fail-stops and dropped frames all happened over its 40
  seeds. Each five-seed slice here asserts crashes only.
- The assembly tests check the reference image's digests against constants. `fleet_assemble_0`
  renders the reference and checks those constants against it.

## Running

On a loaded machine some programs run past sexc's default 60 s test timeout. Use
`sexc test @ajax/mesh-jobs-sim --timeout 600`. `MESH_CHAOS_SEEDS=400` runs the Rust's ignored
400-seed chaos variant in `fleet_chaos_8`.

## Licence

MIT. See the repository's LICENSE.
