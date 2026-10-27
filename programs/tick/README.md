# @ajax/mesh-prog-tick

The fleet scheduler's test program: a periodic counter whose view a migration must carry. View: `0x0`
magic "TICK", `0x4` ticks, `0x8` inits, `0xc` the last verify's mismatches, then the pattern (word i is
P(i) after init; tick t overwrites word t mod W with P(t mod W) ^ (t + 1)). Op 3 verifies 4096 sampled
words and the last 64 ticks' words. `variants/` (tick-w, tick-m, tick-r, tick-big) are the same image
under other manifests.

- **Ported from:** alpha-rust-mesh `programs/tick/src/lib.rs` (and its `variants/`).
- **Tests:** on the simulator: the pattern after init, 100 ticks, verify, a relaunch without OP_INIT (a
  migration) continuing the count, and corrupted words found (once, or twice when sampled and recent).
