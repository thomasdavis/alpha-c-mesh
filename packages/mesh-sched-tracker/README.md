# @ajax/mesh-sched-tracker

The fleet scheduler's rebalancer and the leader's moves. A program moves (`mst_step`) only when its best
alternative scores `improve` (25 %) better than where it is, for `hold_ms` in a row, at least `dwell_ms`
after its last move, one migration at a time and at most `per_min` a minute; an operator's pin moves it
at once (with its view). The owner policy (`mst_owner`, HTCondor's) marks a member busy while other
tenants load it, with hysteresis. A lockstep group is never moved: `mst_group_step` proposes a re-form.
Moves live in the document (`mst_start_move`, `mst_end_move`), so a new leader takes over a migration
the old one started (`mst_resume_moves`), and a move's verdict (`mst_mig_verdict`) is its own programs'
outcomes on the target, not the target's whole reconcile pass.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/sched.rs`: `Move`,
`Tracker` (`owner`, `recent`, `moved`, `step`, `group_step`), `unit`, `start_move`, `resume_moves`,
`MigOutcome`, `mig_verdict`, `end_move`.

## Tests

`tests/tracker.c`: the Rust's `colocation_and_pins`, `hysteresis_and_budget`,
`owner_policy_has_hysteresis`, `groups_are_proposed_not_moved`, `a_new_leader_takes_over_a_migration`,
`the_moves_verdict_is_its_programs_not_the_whole_pass`, the scheduler's half of
`a_pin_never_splits_a_colocated_pair`, and `simulation_is_stable_and_valid` (10 simulated minutes:
members leave and return, the load oscillates; the placement stays valid and nothing flaps).

## Licence

MIT. See the repository's LICENSE.
