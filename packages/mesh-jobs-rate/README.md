# @ajax/mesh-jobs-rate

A member's measured rate for a job program, as the leader keeps it: items per second as an EWMA over
windows of at least 400 ms. A window restarts after the member sat idle longer than `IDLE_MS`, and a
first rate comes from a lease's turnaround. The lease target grows by slow start: a full-size lease
done in under half `LEASE_MS` doubles it (up to `LEASE_MAX`), and one over three times `LEASE_MS`
halves it. `mj_lease_time` is how long a lease should take at a rate.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `IDLE_MS`, `LEASE_MS`, `LEASE_MAX`, `Rate` (`count`, `busy`,
`lease_done`), `lease_time`.

## Tests

`tests/rate.c`: windows and the EWMA, the first rate, slow start both ways and at its cap, idle
restarts, lease times. Every value is as jobs.rs computes it. The full dispatch is checked against
the Rust in `@ajax/mesh-jobs-node`'s traces.

## Licence

MIT. See the repository's LICENSE.
