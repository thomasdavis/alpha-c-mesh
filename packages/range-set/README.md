# @ajax/range-set

A set of u64 items kept as disjoint, non-touching half-open ranges `[s, e)` in order: insert (merging
what overlaps or touches), remove (splitting), contains, union, subtract, and saturating lengths. The
fleet uses it to count the bytes of a migrated view that arrived (a duplicate chunk counts once); the
jobs use it for chunk sets.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/jobs.rs`: `Ranges`.

## Tests

`tests/ranges.c`: the edge cases, and 200 rounds of random inserts and removes checked against a bitmap.

## Licence

MIT. See the repository's LICENSE.
