# @ajax/mesh-jobs-merge

How a new leader of the job table merges a majority's answers to its pull. Any two majorities meet,
so the merge holds every committed entry without matching logs, and the order of application decides
between two results of one item:

1. The committed tables are applied first, the latest commit point first.
2. Then the entries not committed yet are applied, the latest leadership's first.

A committed result is therefore never overwritten by one that was not committed, and between
uncommitted entries the later leadership's wins (review finding 5). `mj_answer` is one member's
answer as it arrives in numbered parts. `mj_answer_of` builds the new leader's own answer from its
replica.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `Answer` (`of`), `merge`.

## Tests

`tests/merge.c`: 100 random merges of up to four answers (committed tables at random commit points,
entries of random leaderships, a later answer from the same member replacing an earlier one), each
giving the same table as the Rust's (golden hashes from the Rust), and an answer built from a replica.

## Licence

MIT. See the repository's LICENSE.
