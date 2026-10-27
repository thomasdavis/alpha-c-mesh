# @ajax/mesh-load-share

The GPU share maths of the load sampler: node load record deltas, share, slices, gaps, and nearest-rank quantiles.

```
@ajax/mesh-load-share: the GPU share a program's nodes obtained (load.rs Nl,
Share, share, quantile): from the node load records' counters (mesh-abi NL_*:
time observed, time lost to someone else, gaps, queue delay), summed over a
program's nodes and over a window of samples.
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/load.rs: `Nl` (delta, add), `Share`, `share`, `quantile`.

## Tests

- `tests/load_share.c`: load.rs's `gpu_share_maths`.

## Differences

- `Nl::read` (from the mailbox) lives with the program sampler.

## Licence

MIT. See the repository's LICENSE.
