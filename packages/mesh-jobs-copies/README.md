# @ajax/mesh-jobs-copies

Who keeps the durable copies of a job item's payload before its result is sent: the member that ran
it, the leader, then the next members after it (in id order, wrapping) that are alive, up to
`COPIES` (3) distinct members. A member whose store refused (full) is skipped, but the leader never
is. With no leader there is no answer. A committed payload therefore survives the loss of any two
members, the leader among them. Also the resend times of pushes and fetches, and `PARKED_MAX`.
Header-only and freestanding.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `COPIES`, `PUSH_RESEND_MS`, `FETCH_RESEND_MS`, `PARKED_MAX`,
`copies_for`.

## Tests

`tests/copies.c`: the Rust's `copies` test.

## Licence

MIT. See the repository's LICENSE.
