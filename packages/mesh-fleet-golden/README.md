# @ajax/mesh-fleet-golden (private, not published)

Golden vectors from the Rust fleet, and the tests that hold the C port to them. `data/` was written by
a scratch Rust program built against copies of alpha-rust-mesh's `fleet.rs`, `sched.rs`, `place.rs`
and `cluster.rs` (rustc 1.88):

- `floats.txt`: 1800 f32 and f64 values: Display (`{}`) and `{:.0}`..`{:.3}`, `{:+.2}`;
- `needs.txt`: 500 random `Needs`: the document token and the `sched` summary;
- `sched.txt`: 150 random fleets (2..6 members of every kind, up and down, loaded, with failures and
  measurements) and documents (1..8 programs with random needs, placements, pins, migrations, a
  lockstep group): the placement pass's document, changes and explains; every member's filter reason
  or score (the f64 total bit for bit, and its explain); then twelve ticks of the owner policy, the
  rebalancer, group re-form proposals and moves. Inputs are exact (floats as bit patterns); expected
  outputs are (length, FNV-1a 64).
- `sim.txt`: the fleet core over the Rust tests' simulated network (partitions, crashes, restarts,
  loss, full disks), 80 seeds: every leadership, every committed version and its document's hash, the
  fail-stops, the clock and each member's final state. The same RNG and core make the same trajectory.

What this caught: Rust's shortest float Display rounds ties up where printf rounds to even, and Rust's
float `sum()` starts at -0.0 (an empty group scores "-0.000").

## Tests

`tests/floats.c`, `tests/needs.c`, `tests/sched.c`, `tests/sim.c` (all pass: every scenario and seed matches).

## Licence

MIT. See the repository's LICENSE.
