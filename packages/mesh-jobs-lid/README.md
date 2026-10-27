# @ajax/mesh-jobs-lid

The job table's leadership ids. A lid is the fleet term in its high 24 bits and a count below it.
`mj_lid_next` picks one above every leadership a member has persisted, so a later leadership's lid
is always larger, even within one term (a restarted coordinator, a leader that leads again), and
never comes from a clock (review finding 10). `mj_lease_base` is the first lease id of a leadership,
so two leaderships' leases never share an id. Header-only and freestanding.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `lid_term`, `lid_next`, `lease_base`.

## Tests

`tests/lid.c`: the checks in the Rust's `a_restarted_coordinator_with_its_clock_behind`, and the
24-bit term limit.

## Licence

MIT. See the repository's LICENSE.
