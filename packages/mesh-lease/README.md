# @ajax/mesh-lease

The lease, ported from alpha-rust-mesh's `mesh-host/src/lease.rs`: mesh-rt's node loop played by a host
thread against the mailbox, for executors without resident kernels. In this port that is the wasm
executor's lanes (`@ajax/mesh-exec-wasm`: one lane per node); in the Rust, Metal's dispatches too
(Metal isn't ported).

A lane takes published tickets in order by CAS on `P_RING_HEAD` (several lanes share a slot's ring
without a lock), hands them to its runner, completes each exactly as mesh-rt's nodes do (`P_RESULTS`,
`P_MARKS`, `P_DONE`, the node record, a trace record), heartbeats while idle, keeps the slot's first
fault, and halts on `P_SHUTDOWN` once nothing is published. `mesh_round` is the lockstep exchange with
its bounded wait. The Rust's `Runner` trait is a table of functions; its `Arc`s (`done`, `stats`) are
pointers the caller keeps alive.

Tests: `tests/lease.c`, the Rust's three tests one for one (lanes sharing a ring and halting, a gated
lane waiting for the blink, the bounded exchange).

## Differences

None in behaviour. Ownership: nobody in the port plan owned `lease.rs` (it served Metal and wasm); the
wasm area ported it.
