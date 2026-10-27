# @ajax/pathtrace-tile

A tile's columns and whole tiles: a column's pass sums and digest (`column_pass`), the fold of one pass
into the accumulators followed by the tone map (`fold_column`, `retone`), and the same over a tile on one
thread (`tile_pass`, `tile_fold`, `tile_retone`) — one column a GPU lane, or 32 columns in a loop on a CPU
or wasm thread. The accumulation order is fixed, so a tile rendered pass by pass or all at once has the
same bits.

Also the two headers that make the renderer usable: `pathtrace/instantiate.h` (define the float contract
of `@ajax/pathtrace-math`, include this, get every function) and `pathtrace/declare.h` (the prototypes for
one suffix). Template headers, freestanding.

**Ported from** `crates/pathtrace-core/src/lib.rs` (`col_digest`, `column_pass`, `fold_column`, `retone`,
`tile_pass`, `tile_fold`, `tile_retone`). **Tested** through the renderer golden tests.

## Differences

- Rust's `(u64, u64)` digest pairs are out parameters.
