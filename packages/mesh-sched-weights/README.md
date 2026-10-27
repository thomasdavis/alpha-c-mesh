# @ajax/mesh-sched-weights

The fleet scheduler's weights and policy (`msw_weights`): the score terms' weights, the rebalancer's
hysteresis (`improve`, `hold_ms`, `dwell_ms`, `per_min`), the replacement grace, the owner policy and
the reservation reserve. `msw_from_keys` reads a cluster config's `sched_*` keys over the defaults.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/sched.rs`:
`Weights`, `Weights::default`, `Weights::from_keys`.

## Tests

`tests/weights.c`: defaults, overrides, `tick_ms`'s floor of 100, each error message.

## Licence

MIT. See the repository's LICENSE.
