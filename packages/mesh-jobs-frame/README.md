# @ajax/mesh-jobs-frame

The job frames on the peer links, and what they carry:

| Frame | Carries |
|---|---|
| F_JOB_AVAIL | A member's job programs (`mj_prog`) and the leases it holds (`mj_held`) |
| F_JOB_STATE | The replicated table: a delta, a part of a snapshot or replace, its end, a commit point (`ST_*`) |
| F_JOB_ACK | A member persisted entries up to n (or reports a gap) |
| F_JOB_PULL | A new leader asks for a member's table |
| F_JOB_HAVE | The answer in numbered parts: the committed table, the entries not committed yet, the end with its leases (`HAVE_*`) |
| F_LEASE | Items for a member to run (`mj_lease_msg`) |
| F_LEASE_CUT | Drop a lease's items from `cut` on |
| F_RESULT | Finished items (`mj_item_res`, statuses `RS_*`) |

On the wire a frame is its type, a `u32` length (`JOB_HEAD`), then a body that starts with the
leadership id. Parts are numbered and the end says how many there were, so a lost part is never taken
for a whole table (review finding 9).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `Prog`, `Held`, `LeaseMsg`, `ItemRes`, `RS_*`, `ST_*`, `HAVE_*`,
`Frame` (`ty`, `lid`, `encode`, `decode`), `put_held`, `get_held`.

## Tests

`tests/frame.c`: the eleven frames of the Rust's `ops_frames_and_the_merge`, encoded byte for byte as
the Rust encodes them (golden bytes from the Rust), decoded and cloned back. Then decode's answers,
word for word the Rust's, to a bad HAVE kind, an unknown type, trailing bytes and short bodies.

## Licence

MIT. See the repository's LICENSE.
