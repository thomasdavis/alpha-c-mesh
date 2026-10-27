# Phase 2: the brief every builder follows

## Common rules

- Work only in your own worktree `/mnt/donto-data/workspace/alpha-c-mesh-wt/<area>` on branch `port-<area>`.
- Read `AGENTS.md` and `docs/port/abi.md` first. The contract is the `@ajax/mesh-abi-*` packages (already on
  main); use them, never redefine ABI constants.
- sexc: `/mnt/donto-data/workspace/sexc/.sexc/out/release/bin/sexc`. It supports header-only libraries, and
  `"kind": "ptx"` / `"wasm"` and `"targets"` (see `sexc help`, and the sexc repo's llms.txt).
- Rust source (read only): `/mnt/donto-data/workspace/ports/alpha-rust-mesh`.
- **Disk is nearly full.** The root filesystem has ~0.5 GB free and `/tmp` is RAM. Never install toolchains.
  Golden-vector Rust goes under `/mnt/donto-data/scratch/port-<area>/` (create it) with
  `CARGO_TARGET_DIR=/mnt/donto-data/scratch/port-<area>/target`, using `cargo +1.88.0`. Crates that need
  wasmtime (mesh-host as a whole) won't build on 1.88: copy the pure-logic Rust modules you need into a
  small scratch crate instead. Delete your scratch target dirs when done.
- Atomic packages (AGENTS.md rule 2), faithful (rule 3), publishable (rule 4): README per package (what,
  ported from, tests, differences), full sexc.json metadata.
- Port the Rust unit tests of your area. Golden vectors from the Rust where there is logic to compare.
- Both `sexc test` and `CC=clang sexc test --profile asan` pass before you finish.
- Commit on your branch as you go, interfaces (headers) first, messages ending
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Other builders may `git merge port-<area>`.
- You may `git merge` other builders' branches when you need their packages. Don't edit packages another
  area owns; if you need a change there, write it in your report.
- Write `docs/port/<area>.md`: Rust file/item → C package/symbol, the package graph, what's verified how.
- Don't push, don't publish, don't deploy. There is no GPU here; never contact other machines or pods.
- Final report: packages (name: one line), interfaces others must know, test counts, what isn't done.

## Who owns what

| Area | Owns (packages/ unless noted) | Rust |
|---|---|---|
| `ref` | `mesh-ftz*`, `mesh-ref-*`, `pathtrace-*` | mesh-ref, pathtrace-core |
| `device` | `mesh-rt-*` (the device runtime), `programs/<name>` (GPU programs, `kind: ptx`, `@ajax/mesh-prog-<name>`), CPU builds of the same programs for tests, `apps/mesh-build-programs`, `apps/mesh-ptx-link`, `apps/mesh-ptx-float-check` | mesh-rt, programs/*/src, scripts/build-programs.sh, ptx-link.sh, ptx-float-check.sh |
| `wasm` | `wasm-*` (a WebAssembly decoder, validator, interpreter, the float guard), `mesh-rt-wasm-*` (guest runtime), `mesh-exec-wasm*` (meshd's wasm executor), `programs/<name>-wasm` (`kind: wasm`, `@ajax/mesh-prog-<name>-wasm`) | mesh-host/wasm.rs, mesh-rt-wasm, programs/*/wasm |
| `exec` | `cuda-driver*` (dlopen of libcuda), `nvjitlink*`, `mesh-shm`, `mesh-mailbox*`, `mesh-toml` (or a TOML subset parser), `mesh-manifest*`, `mesh-loader`, `mesh-backend*`, `mesh-mps*`, `mesh-inproc`, `mesh-native*`, `mesh-worker*` (the native worker SDK), `mesh-cpuuse`, `mesh-log`, `apps/mesh-test-worker` | cuda.rs, nvjitlink.rs, shm.rs, mailbox.rs, manifest.rs, loader.rs, backend.rs, mps.rs, mps_worker.rs, inproc.rs, native.rs, cpuuse.rs, log.rs, crates/mesh-worker |
| `daemon` | `mesh-load`, `mesh-programs`, `mesh-publish`, `mesh-status`, `mesh-control*`, `mesh-http*`, `mesh-views`, `mesh-net*`, `mesh-infer*`, `mesh-gpt2x-host`, `mesh-shared`, `mesh-driver`, `mesh-args`, `apps/meshd` | load.rs, programs.rs, publish.rs, status.rs, control.rs, http.rs, views.rs, net.rs, infer.rs, inferd.rs, gpt2x.rs, shared.rs, driver.rs, args.rs, main.rs, lib.rs |
| `fleet` | `mesh-fleet*`, `mesh-sched*`, `mesh-place*`, `mesh-cluster*`, `mesh-reconcile*`, `mesh-peer*` | fleet.rs, sched.rs, sched_tests.rs, place.rs, cluster.rs, reconcile.rs, peer.rs, peer_sched.rs |
| `jobs` | `mesh-jobs*`, `mesh-assemble*`, `mesh-blobs*`, `mesh-blobd*`, `mesh-jobd*` | jobs.rs, assemble.rs, blobs.rs, blobd.rs, jobd.rs |
| `top` | `tui-*` (a small terminal UI library: buffer, layout, widgets, input, ANSI backend), `mesh-top-*`, `apps/mesh-top`, the mock meshd | crates/mesh-top |
| later | checks (kcheck, lifecheck, fleetcheck), the remaining scripts as C, Metal | |

## How the daemon fits together

`shared.rs` (the daemon's shared state) and `driver.rs` (the main loop) belong to `daemon`, which also wires
everything into `apps/meshd` at the end. `fleet` and `jobs` port their logic as packages with **explicit
interfaces**: state structs plus functions, with I/O and access to the daemon's state through small
callback structs or plain parameters, not a global. Where the Rust reaches into `shared`, define the
narrowest interface you need in your own package and say in your report how the daemon should wire it.
`exec` provides the execution backends (CUDA, MPS, native, in-process) behind one backend interface that
`wasm`'s executor also implements; `exec` publishes that interface header early.
