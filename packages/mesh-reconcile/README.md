# @ajax/mesh-reconcile

The fleet reconciler: makes this member's programs match the committed desired state, on every commit
and when the member (re)joins (it is the only thing that changes the program set in a fleet). One pass:

1. the plan (`@ajax/mesh-fleet-plan`): unloads, loads, upgrades (or rollbacks), run states and resets.
   A program this member has no image for is reported *unsupported*, not failed; a failure is skipped
   at that hash until the 30 s retry or the next document. Programs migrating here are loaded stopped,
   take their views from the source (producers first) and start (consumers first), and the move's
   outcome (with its gap) goes out on the beat;
2. the lockstep group: a new epoch quiesces, sets the group words, resets and HELLOs; a cycle within
   the epoch quiesces with rounds mirrored and HELLOs only with the document's image and the cycle's
   task done here (review finding 2), never re-running a task that failed;
3. on the leader, the placement requests (one dropped from the document goes back to local).

Everything that touches the driver, the mailbox and the fleet thread goes through `mrc_ops`, which the
daemon wires. `mrc_thread_start` runs it on its own thread (post documents; failures retried after
30 s).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/reconcile.rs`:
`run`, `Rec` (`pass`, `act`, `migrate`, `fetch_view`, `quiesce`, `set_group_words`, `ready`, `group`,
`place`, `status`), `cycle_ready`, `image_ready`.

## Tests

`tests/reconcile.c`: the Rust's `a_cycle_is_hellod_only_with_its_image_and_task`, and passes against a
fake driver: loads, an upgrade, an unload, an unsupported program, a failure skipped and retried, a
migration in the right order, a lockstep epoch and a task cycle, a paused group, placement on the leader.

## Licence

MIT. See the repository's LICENSE.
