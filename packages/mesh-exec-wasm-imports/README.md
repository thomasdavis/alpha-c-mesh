# @ajax/mesh-exec-wasm-imports

The `mesh::*` imports of meshd's wasm executor (the contract is mesh-abi v2::wasm,
`@ajax/mesh-abi-wasm`), ported from alpha-rust-mesh's `mesh-host/src/wasm.rs` (`LaneData`, `linker`,
`copy_out`, `copy_in`): `info`, `ld8/32/64`, `st8/32/64`, `cas32`, `add32/64`, `read`, `write`, `peer`,
`find`, `prog_state`, `count`, `counter`, `input_pending`, `input_pop`, `respond`, `exchange`,
`pipe_push`, `pipe_pop`, `pipe_len`, `host_ns`, `fault`.

A guest has no pointer into the mailbox: every access names a window (its view, the exchange page for the
lockstep program, a peer view resolved for this task under the PT_GEN seqlock and gated by `reads`) and is
bounds-checked exactly as mesh-rt's View is: out of range reads 0, drops writes and records
`FAULT_VIEW_BOUNDS | off << 8` (the first fault kept). Loads acquire, stores release, read-modify-writes
acq_rel. `exchange` runs the lockstep round (`@ajax/mesh-lease`) and re-arms the task's deadline.

Tests: `@ajax/mesh-wasm-programs-check` (`tests/imports.c` drives every import through a guest, with each
fault; the program checks use the rest).
