# @ajax/mesh-exec-wasm

meshd's wasm executor (mesh-abi `BACKEND_WASM`), ported from alpha-rust-mesh's `mesh-host/src/wasm.rs`:
CPU programs as WebAssembly modules running inside meshd beside the GPU backend, behind the backend
interface of `@ajax/mesh-backend` (`mesh_wasm_backend_open`).

- **Compiler** (`prepare`, on the driver's JIT thread): the manifest and abi checks, `<name>.wasm` beside
  `program.toml`, the image hash (the PTX's when the catalog has one, else the wasm's, `param` folded in),
  the float guard (`@ajax/wasm-float-guard`), the compile (`@ajax/wasm-interp`; with `--wasm-cache DIR`,
  `@ajax/mesh-exec-wasm-cache`), and `mesh_rt_abi` read in a scratch instance on a zeroed mailbox.
- **Lanes**: a running slot is `nodes` lease lanes (`@ajax/mesh-lease`), one thread and instance per
  node, one ticket at a time; `mesh_init` / `mesh_task(op, arg)`; state lives in the view, reached
  through the `mesh::*` imports (`@ajax/mesh-exec-wasm-imports`).
- **Isolation**: linear memory capped at `mem_mb`, each task's epoch deadline `task_ms` (an epoch thread
  ticks every ms while lanes run; a lockstep exchange's wait re-arms it). An overrun is `FAULT_TIME`,
  any other trap `FAULT_TRAP | (wasmtime's trap code + 1) << 8`; the lane re-instantiates and the driver
  fails the slot.
- **Lifecycle**: isolated, not resident (`module_scope` is the slot itself); lanes ignore `H_BLINK`;
  `reads_live`. Registers its cleanup at `MESH_CLEANUP_WASM`: running lanes are asked to halt.

## Differences

- The engine is an interpreter (`@ajax/wasm-interp`), not Cranelift. The behaviour is the same — the
  GPUs' digests come out bit for bit (kcheck XG) — but it is about 25–35× slower than the JIT on integer
  code. Manifests' `task_ms` budgets tuned for wasmtime (`sha256d`: 1000 ms for a 2^16-nonce chunk) can
  be too tight here: raise them or use smaller chunks. See docs/port/wasm.md for the measurements.
- No SIMD (wasmtime runs it for IEEE images): such an image is refused at compile ("jit").
- Caps' name says `(wasm-interp)` where the Rust says `(wasmtime 48)`.
- The `--wasm-cache` files hold the module's bytes (no machine code to keep).

Tests: through `@ajax/mesh-wasm-programs-check` (every guest program end to end, the imports, lockstep,
traps, interrupts, memory caps) and `@ajax/mesh-exec-wasm-cache`.
