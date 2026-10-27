# @ajax/mesh-sched-score

The fleet scheduler's feasibility filter and score:

- `msc_filter`: the executor a program would run on at a member, or the first reason it cannot run
  there (down, cordoned, backends, arch, `min_mem_mb`, avoid, a GPU node too big for the member's GPU);
- `msc_score_terms`: the weighted terms of running it there (the processor share it would get times
  its measured speed, CPU headroom after the move, memory headroom, spread or pack, link RTT, prefer,
  stickiness, the owner penalty), and `msc_explain` their text.

The arithmetic keeps the Rust's f32 and f64 steps in the same order, so a C and a Rust leader decide
the same from the same inputs.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/sched.rs`:
`filter`, `eligible`, `Score` (`explain`), `Cx` (`simple`), `score_terms`, `score`.

## Tests

`tests/score.c`: the Rust's `filter_keeps_the_first_reason`, `node_shape_filters_members` (its filter
part) and `scores_follow_share_speed_load_and_prefer`; an explain text checked against the Rust.

## Licence

MIT. See the repository's LICENSE.
