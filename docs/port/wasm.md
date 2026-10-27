# The wasm area

meshd's WebAssembly executor and everything it stands on, in C. The Rust runs wasm CPU programs in
wasmtime 48 (Cranelift) and checks them with wasmparser; this port has its own WebAssembly
implementation, an interpreter, built for exactly what meshd needs.

## Rust → C

| Rust | C package | What |
|---|---|---|
| wasmparser (binary reading) | `wasm-leb128`, `wasm-reader`, `wasm-types`, `wasm-opcodes`, `wasm-instr`, `wasm-module`, `wasm-decode` | LEB128, a bounds-checked reader, the binary format's constants, the instruction table (with wasmparser's operator names), one instruction, the module structs, the section decoder |
| wasmparser `Validator` | `wasm-validate` | module rules and the spec's type check of every body, per enabled proposal |
| Cranelift + wasmtime runtime | `wasm-numeric`, `wasm-lower`, `wasm-fuse`, `wasm-interp` | numeric semantics with wasmtime's canonical NaNs and trap codes; lowering to flat code; superinstructions; the interpreter (store limits, host imports, epoch interrupts) |
| (tests' hand-assembled modules) | `wasm-builder` | modules built from C |
| `wasm.rs` `float_check`, `flushing_f32` | `wasm-float-guard` | the load-time float guard, its Rust test case for case |
| `wasm.rs` `Rt::module`, `evict`, `CACHE_CAP` | `mesh-exec-wasm-cache` | `--wasm-cache` files and eviction |
| `wasm.rs` `LaneData`, `linker`, `copy_out`, `copy_in` | `mesh-exec-wasm-imports` | the `mesh::*` imports |
| `wasm.rs` `engine`, `Rt`, `Program`, `instantiate`, `WasmCompiler`, `WasmBackend`, `Lane`, `trap_fault` | `mesh-exec-wasm` | the executor behind `@ajax/mesh-backend` |
| `lease.rs` | `mesh-lease` | the node loop of lease lanes (nobody owned it: it served Metal and wasm) |
| `crates/mesh-rt-wasm` | `mesh-rt-wasm` | the guest runtime (wasm32) |
| `programs/*/wasm` | `programs/<name>-wasm` | hello, tick, infer, train, hashjob, sha-verify, pipesrc, pipesink-w, sha256d, pathtrace |
| (kcheck W/X/XG, lifecheck wasm checks) | `mesh-exec-wasm-testkit`, `mesh-wasm-programs-check`, `programs/probe-wasm` | the executor run on a test mailbox; every guest and import checked |

Package graph (each arrow: depends on):

```
wasm-leb128 <- wasm-reader <- wasm-instr <- wasm-decode <- wasm-validate <- wasm-lower <- wasm-fuse
wasm-types  <- wasm-opcodes <-'              wasm-module <-'                                  |
wasm-numeric <------------------------------------------------------------ wasm-interp <-----'
wasm-interp, wasm-float-guard, mesh-lease, mesh-mailbox, mesh-backend, mesh-loader, mesh-manifest
    <- mesh-exec-wasm-imports <- mesh-exec-wasm <- mesh-exec-wasm-testkit <- mesh-wasm-programs-check
mesh-abi-*, mesh-ftz, mesh-ref-*, pathtrace-* <- mesh-rt-wasm <- programs/*-wasm   (wasm32)
```

## How it is verified

- **The interpreter**: spec-style tests of every trap (with wasmtime's codes), bounds, call_indirect,
  br_table, multi-value, bulk memory, tables, canonical NaNs, imports, store limits, interrupts and the
  start function; a **differential** test, the same C compiled natively and to wasm32 by clang, equal for
  40 seeds of integer and float work; **9000 fuzzed modules** (mutated and random) refused or run, never
  crashing (ASan); the decoder against every truncation of a module and the format's malformed cases;
  the validator against type, index, structure and disabled-proposal errors.
- **The float guard**: the Rust's `float_guard` test case for case, plus every real guest at load.
- **The lease**: the Rust's three tests one for one.
- **The guests, end to end in the executor**:
  - **train and infer reproduce the GPUs' digests** (kcheck XG: samples `88ad9ee53d7f193d`, checkpoints
    `10e3ab33652a552e`, params `48104fa8535ef973`, losses `e511adc40fd1a525`, infer `6612dd70d4a88dc4`,
    the same on dgx-00, being and the Mac in the Rust's measurements) over the full 4096-task run
    (`MESH_FULL=1`; ~2 minutes in a release build), and match the CPU reference bitwise after every task;
  - train in a lockstep group of 2 (rounds, a timeout with T_ERR/T_PENDING, the resume, the deadline
    re-armed by the wait);
  - pathtrace tiles equal the CPU reference (`@ajax/pathtrace-z`) bit for bit: digests, rgb, pass sums;
  - sha256d finds Bitcoin's genesis block in a leased chunk (nonce, hash, record), its shares at a low
    target re-hash correctly, sha-verify re-hashes them through `reads`, and the request language answers;
  - hashjob items against the CPU's sha256d; tick; pipesrc → pipesink-w over a pipe, every digest checked;
  - hello's hooks: per-access and bulk host calls, a trap (`FAULT_TRAP | 10 << 8`), an interrupt at
    `task_ms` (`FAULT_TIME`), memory growth stopped at `mem_mb`;
  - every `mesh::*` import and its faults through `programs/probe-wasm`.
- `sexc test` and `CC=clang sexc test --profile asan` pass.

## Speed

An interpreter, not a JIT. Measured on this box (8 cores at load average ~30 from other jobs, so absolute
numbers are inflated several-fold; ratios are what matter):

| | C interpreter | Rust (wasmtime, from its README) |
|---|---|---|
| clang -O2 C, interpreted vs native | integer 36× slower, float 9× (fused) | Cranelift is near native |
| hello publish → result | 2–5 µs | 7.5 µs (being), 0.6–0.8 µs (dgx-00) |
| hello tasks/s, one lane | ~1.2 M | — |
| one `mesh::ld32` / a 4 KiB `mesh::read` | ~90 ns / ~2.5–3.5 µs | 7.4 ns / 477 ns (being) |
| sha256d, one lane | 0.022 MH/s (2^16 chunk in 3.0 s) | 2.2 MH/s (being, one lane) |
| train X (4096 tasks × 64 steps) | ~120 s | 1.73 s (being) |

So: **behaviour identical, speed 25–100× lower than Cranelift** depending on the code (host calls and
call-heavy code worst). `task_ms` budgets written for wasmtime can be too tight; sha256d's 1000 ms for a
2^16 chunk is (raise it, or use a smaller `param`). A register-based lowering or a JIT would close the gap;
neither is done.

## Differences from the Rust

- The engine (above), and no SIMD: wasmtime runs SIMD in IEEE images; here they are refused at compile
  ("jit"). wasmparser type-checks SIMD; `wasm-validate` walks it untyped (the guard's outcome is the same).
- The `--wasm-cache` keeps the module's bytes, not machine code (the flag, files, logs and eviction are
  the same; a hit saves nothing).
- Caps' name says `(wasm-interp)` for `(wasmtime 48)`.
- `mesh-lease` was ported here (no owner in the plan).

## Notes for other areas

- **Catalog**: a program's wasm image is the `@ajax/mesh-prog-<name>-wasm` package's build
  (`.sexc/out/<profile>/wasm/ajax-mesh-prog-<name>-wasm.wasm`); the catalog tool must copy it to
  `<catalog>/<name>/<name>.wasm` beside `program.toml`.
- **sexc**: a host package can't depend on a wasm package, so the program checks find the guests beside
  their own binary (`sexc build` first). An artifact dependency (build the wasm package first, give its
  path to tests) would fix that.
- **Debug builds**: `wasm-interp` and `mesh-exec-wasm-imports` set `-O2` in their own cflags (the
  sanitizers still apply): at -O0 the interpreter is too slow for any guest to meet a task budget.
