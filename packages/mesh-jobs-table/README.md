# @ajax/mesh-jobs-table

The job table: every job with what is known of it (done items, results, runs of done items without
results, failures, retries counted, duplicates, cancel and finish, per-member counts, the fold of an
endless job's runs, its presentation word), changed only by applying ops (@ajax/mesh-jobs-op).

The table is a join-semilattice: every op is idempotent, and applying two tables' ops in either order
gives their union, in which an item's first result wins. That is why a new leader of the fleet can
merge a majority's tables without matching their logs (`@ajax/mesh-jobs-merge`).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `FIN_*`, `Job` (`fold`, `total`, `settled`, `lease_of`, `active`,
`complete`, `state`, `left`), `Table` (`apply`, `ops`, `next_id`).

## Tests

- `tests/table.c`, the Rust's `ops_frames_and_the_merge`, table half: the ops round trip,
  idempotence, the union in either order, a table rebuilt from its own ops, and `lease_of`.
- `tests/table.c`, golden: four random sequences of 600 ops (every kind, folds and forgets included).
  The identical generator runs in the Rust, and every 50 ops the hash and length of the encoded table
  match the Rust's. So does the table rebuilt from its own ops, and whether it equals the original.
  Then the whole sequence and `next_id` are compared.

## Licence

MIT. See the repository's LICENSE.
