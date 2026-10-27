# @ajax/mesh-fleet-plan

What makes one member's programs match the fleet's committed document (`mfl_plan`): unloads first (they
free slots and the view heap), then loads, upgrades, rollbacks (when the retained image is the one
wanted) and migrations (the target loads it stopped and takes the source's view), then run states and
resets. And a fresh fleet's first document (`mfl_seed`): the first leader's programs, with a group of
every eligible up member for the lockstep program.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/fleet.rs`:
`Actual`, `Action`, `plan`, `seed`.

## Tests

`tests/plan.c`: the Rust's `reconcile_plans`, case by case.

## Licence

MIT. See the repository's LICENSE.
