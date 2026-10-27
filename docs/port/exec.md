# Port: meshd's execution backends (area `exec`)

The executors meshd's driver runs programs on, and what they stand on: the backend interface, the
manifest and the loader, the CUDA driver and nvJitLink, the shared mailbox, MPS, the in-process
driver, and native worker processes with their SDK. 31 packages and one app.

## Rust → C

| Rust (crates/mesh-host/src unless noted) | C package | Main symbols |
|---|---|---|
| `backend.rs` (`Backend`, `Compiler`, `Kernel`, `Stream`, `Prepared`, `Caps`, `pick_exec`, `runs_here`, `prepare_any`, `check_manifest`, `label`, `cleanup`) | `mesh-backend` | `mesh_backend` / `mesh_backend_ops`, `mesh_compiler`, `mesh_kernel`, `mesh_stream`, `mesh_prepared`, `mesh_caps`, `mesh_pick_exec`, `mesh_runs_here`, `mesh_prepare_any`, `mesh_check_manifest`, `mesh_backend_label`, `mesh_backend_cleanup` / `_on_cleanup` |
| `backend.rs` (`co_resident`, `room`) | `mesh-occupancy` | `mesh_co_resident`, `mesh_occ_room` |
| `manifest.rs` (`Manifest`, `Field`, `Window`, `Place`, `Inbox`, `parse`, `load`, `threads`, `smem`, `native`) | `mesh-manifest` | `mesh_manifest`, `mesh_manifest_parse`, `mesh_manifest_load`, `mesh_manifest_place_*`, `mesh_manifest_debug` |
| `manifest.rs` (`Val`, `value`, `quoted`, `int`, `pair`, `strip_comment`) | `mesh-toml` | `mesh_toml_value`, `mesh_toml_int`, `mesh_toml_pair`, `mesh_toml_strip_comment` |
| `manifest.rs` (`Image`, `encode_entry`, `encode_ext`) | `mesh-manifest-encode` | `mesh_image`, `mesh_manifest_encode_entry`, `mesh_manifest_encode_ext` |
| `sched.rs` (`Replicas`, `Migrate`, `Term`, `Needs`; not `Term::matches`) | `mesh-manifest-needs` | `mesh_needs`, `mesh_term_parse`, `mesh_term_word`, `mesh_migrate_word` |
| `loader.rs` (`Payload`, `catalog_name`, `resolve`, `read`, `image_hash`, `identity`, `check_hash`, `catalog`), `native.rs` (`identity`) | `mesh-loader` | `mesh_payload`, `mesh_catalog_name`, `mesh_loader_resolve`, `mesh_loader_read`, `mesh_image_hash`, `mesh_native_identity`, `mesh_loader_identity`, `mesh_check_hash`, `mesh_catalog` |
| `loader.rs` (`Saved`, `read_state`, `write_state`) | `mesh-state-file` | `mesh_saved`, `mesh_state_read`, `mesh_state_write` |
| `log.rs` | `mesh-log` | `mesh_info`, `mesh_error`, `mesh_log_emit`, `mesh_log_dropped`, `mesh_log_flush`, `mesh_log_exit`, `mesh_log_recent` |
| `cpuuse.rs` | `mesh-cpuuse` | `mesh_cpuuse_slots`, `mesh_meter_*`, `mesh_thread_cpu_ns` |
| `shm.rs` | `mesh-shm` | `mesh_shm_new`, `mesh_shm_path` |
| `mailbox.rs` | `mesh-mailbox` | `mesh_mailbox`, `mesh_mailbox_get` / `set` / `publish` / `cas` / …, `mesh_mailbox_words` |
| cudarc's driver API (as `cuda.rs` calls it) | `cuda-driver` | `mesh_cuda_fns`, `mesh_cuda_load`, `mesh_cu_ck` |
| (test support) | `cuda-driver-fake` | a simulated device: `mesh_fake_cuda_install` |
| `cuda.rs` | `mesh-cuda` | `mesh_cuda_gpu`, `mesh_cuda_kernel`, `mesh_cuda_stream`, `mesh_cuda_limits`, `mesh_cuda_force_eager`, `mesh_cuda_use_lib`, … |
| `nvjitlink.rs` | `nvjitlink` | `mesh_nvjitlink_open`, `mesh_nvjitlink_ptx_to_cubin` |
| `mps.rs` (frames) | `mesh-mps-wire` | `MESH_W_*`, `mesh_mps_read_frame`, `mesh_mps_write_frame` |
| `mps.rs` (the rest) | `mesh-mps` | `mesh_cuda_backend_open`, `mesh_mps_check_native`, `mesh_mps_native_pipe_dir`, `mesh_mps_cleanup` |
| `mps_worker.rs` | `mesh-mps-worker` | `mesh_mps_worker_main` |
| `inproc.rs` | `mesh-inproc` | `mesh_dev`, `mesh_mb2`, `mesh_inproc_load`, `mesh_prog_*`, `mesh_heap`, `mesh_reads_mask` |
| `native.rs` (`spawn`, `pipe`, `describe`, `exit_word`) | `mesh-spawn` | `mesh_spawn`, `mesh_pipe_cloexec`, `mesh_describe_status`, `mesh_exit_word` |
| `native.rs` (`usage`) | `mesh-proc-usage` | `mesh_proc_usage` |
| `native.rs` (`Limits`, `limits_available`, `check_cgroup`) | `mesh-native-limits` | `mesh_native_limits`, `mesh_native_limits_available`, `mesh_native_check_cgroup` |
| `native.rs` (the rest) | `mesh-native` | `mesh_native_backend_new`, `mesh_native_worker_*`, `mesh_native_host_*`, `mesh_native_cleanup` |
| crates/mesh-worker `lib.rs` (`Mb`, `View`, `Ctx`) | `mesh-worker-ctx` | `mesh_view_*`, `mesh_worker_ctx`, `mesh_ctx_*` |
| crates/mesh-worker `lib.rs` (`node_loop`, `run_nodes`) | `mesh-worker-node` | `mesh_worker_node_loop`, `mesh_worker_run_nodes` |
| crates/mesh-worker `lib.rs` (`run`) | `mesh-worker` | `mesh_worker_run`, `mesh_worker_panic` |
| crates/mesh-worker `src/bin/test_worker.rs` | `apps/mesh-test-worker` | the fixture |
| Rust's `format!` (the parts messages use) | `mesh-text` | `mesh_text`, `mesh_errf`, `mesh_text_debug_str` / `_f64` / `_f32`, `mesh_trim`, `mesh_parse_u32` / `_f64` |

Not ported: `metal.rs`, `lease.rs` (Metal's lease engine) and `mesh-rt-metal` (Metal is C++); the wasm
executor `wasm.rs` belongs to area `wasm` and implements `mesh-backend`'s interface.

## The interface, for the other areas

- **Errors** are `char **err` (malloc'd; the caller frees): every function that can fail returns `bool`
  (or NULL) and sets `*err` to the Rust's message, byte for byte, built with `mesh-text`.
- **The backend** (`mesh/backend.h`) is the Rust's traits as tables of functions: `mesh_backend { ops,
  self }`, `mesh_compiler`, and `mesh_kernel { ops, self }` for a loaded image (an executor provides its
  own `mesh_kernel_ops`, with `exec` = its BACKEND_*), `mesh_stream` for where a slot's work runs. The
  trait's default methods apply when an op is NULL (`crashed`, `owns_halt`, `reads_live`, `sample`,
  `online_timeout_ms`). Ownership: `prepare` takes the payload; `load` takes `prepared->kernel` (mps,
  native, wasm) or loads the code (blink) and returns a new heap kernel; `unload` frees the kernel.
- **Cleanup**: executors register their exit cleanup with `mesh_backend_on_cleanup(order, fn)`;
  `mesh_backend_cleanup()` runs them native first, then wasm, then MPS (as backend.rs's `cleanup`).
- **The manifest** carries `mesh_needs` (the scheduler's keys) parsed and validated here;
  `Term::matches` needs the scheduler's member type and is the fleet area's.

## How the daemon wires it (for area `daemon`)

1. At start, first in `main`: `mesh_cuda_use_lib(mesh_cuda_lib_dir(argc, argv), argc, argv, &err)` (may
   re-exec), then `mesh_cuda_force_eager`. `meshd worker …` dispatches to `mesh_mps_worker_main`.
2. The mailbox: `mesh_shm_new`; the GPU backend: `mesh_nvjitlink_open(--nvjitlink or MESH_NVJITLINK_DIR)`
   then `mesh_cuda_backend_open(iso, sms, mps_dir, pool, &shm, &jit, &b, &err)`.
3. Native workers beside it: `mesh_native_backend_new(&shm, logs, &cfg)` with `cfg.gpu` =
   MPS (`cfg.mps_pipe_dir = mesh_mps_native_pipe_dir()`, `cfg.check_gpu_client = mesh_mps_check_native`),
   BLINK, METAL or NONE (`--backend wasm`), and `cfg.env` (MESH_SERVICE_BASE, MESH_MODELS, MESH_SYSTEM).
4. The JIT thread gets `mesh_compiler_entry[]` (the GPU's first) and calls `mesh_prepare_any`.
5. Every exit path calls `mesh_backend_cleanup()`, then `mesh_log_exit`.
6. Native stats for the control words: `mesh_native_worker_stats(mesh_kernel_native(k))`; machine
   totals: `mesh_native_host_totals(mesh_native_backend_host(b))`.

## How it is verified

- **Against the Rust's outputs.** The manifest parser, its Debug output and its encodings are compared
  with what the Rust printed for 5696 manifests (every `program.toml` of alpha-rust-mesh, the Rust's own
  test cases, and mutations of every key): identical. `mesh-text`'s formatting is compared with Rust
  1.88's `{:?}` of floats and strings.
- **The Rust's unit tests**, ported for: loader, backend (`co_residency`), cpuuse, shm, mps (`frames`),
  native (identity, formats, usage, limits, status lines), mesh-worker (three tests) and the whole
  native lifecycle suite (crates/mesh-worker/tests/lifecycle.rs), including the systemd memory-cap OOM
  kill and the CPU cap, which ran here.
- **Without a GPU.** `cuda-driver-fake` simulates the device behind the driver table; its launched
  kernels play the program's node loop on the real mailbox. The CUDA backend runs on it in blink mode,
  and `--isolation mps` runs for real: the MPS test binary is also a fake `nvidia-cuda-mps-control` and
  the `meshd worker` processes, so the daemon, the pool, preloads, launches through the wire format and
  the cleanup all run.
- **Needs a real GPU**: every call to the actual CUDA driver and nvJitLink (their ABI is taken from
  cudarc's CUDA 13.0 bindings: struct layouts, enum values and symbol names), green contexts on real
  hardware, MPS client stalls and timings, and libcuda's `--cuda-lib` re-exec.
- Every package builds and tests clean with gcc and with clang under `--profile asan`.
