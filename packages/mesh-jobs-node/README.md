# @ajax/mesh-jobs-node

A meshd's job node, sans IO. It holds the member's replica of the fleet's job table, runs the
leader's dispatcher when it leads, and handles the member's side of the leases. Every input returns
outputs for the caller to make durable and act on, in order. The inputs are: the fleet's view, a
frame from a member, a finished item, a stored or acknowledged blob, a change request, and a tick.

- **Replication.** The leader appends deltas every tick, persists them and sends them to every
  member, which persist them and acknowledge. An entry is committed once a majority persisted it.
  Reads show committed entries only. A new leader pulls a majority's tables, merges them
  (`@ajax/mesh-jobs-merge`), commits the merge as its first entry, and sends snapshots to members
  that need them. Transfers come in numbered parts.
- **Dispatch.** Each member that runs the program gets leases sized to `LEASE_MS` at its measured
  rate, after a probe, with slow start. A member gets the next lease when half of its current one is
  done, pipelined two deep. With nothing left to lease, an idle member steals the tail of the lease
  with the most time left. Who gets leases is `@ajax/mesh-jobs-select`.
- **Failures.** The items of a lost member, an expired lease or a cut lease go back to the queue.
  Results are recorded once per item, so committed results are exactly-once, and later duplicates
  are counted and dropped. A failing item is retried up to `attempts`. Deadlines, cancels and
  forgets are handled. A submit for a program nobody runs yet waits `SUBMIT_WAIT_MS`.
- **Payloads.** A result with a payload is staged until its copies are durable
  (`@ajax/mesh-jobs-copies`). The leader commits it only while it holds the blob itself; otherwise
  it parks the result and fetches the blob.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `Node` and its inputs, `Cfg`, `View`, `Lease`, `Queue`, `Disp`,
`Lead`, `Staging`, `Staged`, `SchedCx` (now `mj_sched`), `RESEND_MS`, `AVAIL_MS`, `HOLDOFF_MS`,
`EXPIRY_TRIES_MAX`, `RESULTS_MAX`, `SUBMIT_WAIT_MS`, `lease_time`, `fleet_members`, `cancel`.

## Tests

- `tests/node.c`: the Rust's direct node tests:
  - `a_snapshot_keeps_the_entries_acknowledged_past_it` (the node half);
  - `a_table_with_a_part_lost_is_not_installed`;
  - `no_snapshot_before_the_merge_commits`;
  - `an_answer_packs_its_entries`;
  - `nothing_leaves_without_its_persist`.
- `@ajax/mesh-jobs-sim`'s `trace.c`: the node checked output for output against the Rust's, in seven
  scenarios of up to five members (crashes, restarts, failing disks, dropped frames, hung members,
  clock jumps, payloads, forgets).
- `@ajax/mesh-jobs-sim`'s `fleet.c`: the Rust's fleet tests over the fleet's election core.

## Licence

MIT. See the repository's LICENSE.
