# @ajax/mesh-jobs-out

What the job node asks of the world (`mj_out`):

- send a frame to a member;
- push, fetch or let go of a payload blob (blobd);
- persist a log record;
- run or cut a lease (the local driver);
- answer a change by its token;
- write a log line.

`mj_durable` puts the outputs in their durable order. Every persist is appended, and the log is
synced before the first send or answer after it, so no acknowledgement, delta or answer ever leaves
before what it promises is on disk. The first append or sync that fails stops everything after it
(fail-stop, review finding 4). Where the log goes is an `mj_disk`: jobd's store, or a test's faulty
disk.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `Out`, `Disk`, `durable`.

## Tests

`tests/out.c`: the order of appends and syncs around sends and answers, a failing append, and a
failing sync. The node half of the Rust's `nothing_leaves_without_its_persist` is in
`@ajax/mesh-jobs-node`.

## Licence

MIT. See the repository's LICENSE.
