# @ajax/pathtrace-view

The view of programs/pathtrace: the byte offsets the program, kcheck, mesh-top and the http program agree
on — the header words, job records, the request and response windows, the scene, per-tile meta, the
payload ring, the tile-major pixels and accumulators — plus the executors and the ops (`OP_RESTART`,
`OP_TONEMAP`, `OP_DEFAULT`, `OP_PASS`, `OP_SET`). Header only, freestanding, with the Rust's
compile-time layout checks as static asserts.

**Ported from** `crates/pathtrace-core/src/lib.rs`, module `view`. **Tested**: the derived offsets are
pinned to values printed from the Rust.
