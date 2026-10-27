# Port: meshd's daemon (area `daemon`)

meshd itself: the driver thread, the shared state and its messages, the control and HTTP ports, the
load and network telemetry, inference requests, the command line, and `apps/meshd`, which wires the
other areas' executors, fleet and jobs in. 29 packages and one app; everything that runs without a GPU is
tested, a three-member fleet of whole meshds included.

## Rust → C

| Rust (crates/mesh-host/src) | C package | C |
|---|---|---|
| `driver.rs` | `mesh-driver` | `mesh_driver_new/start/run/free`, `mesh_reads_mask`, `mesh_setup_state_*` |
| `programs.rs` (`Heap`, `pages`) | `mesh-view-alloc` | `mesh_view_heap_*`, `mesh_view_pages` |
| `programs.rs` (the table writer) | `mesh-prog-table` | `mesh_pt_write_entry`, `_set_state`, `_clear`, `_write_pipe`, `_set_hold`, `_flight*`, `_reset_slot`, `_name_of`, `_find`, `_read_entry` |
| `programs.rs` (`Image`, `Slot`, `Phase`) | `mesh-driver` | `dimage`, `dslot`, `dphase` (internal) |
| `publish.rs` | `mesh-publish` | `mesh_pring_*`, `mesh_due`, `mesh_publish_auto`, `mesh_publish_parts` |
| `status.rs` (`HostStatus`) | `mesh-host-block` | `mesh_host_block_*` |
| `status.rs` (`snapshot`) | `mesh-snapshot` | `mesh_snapshot` |
| `shared.rs` | `mesh-shared` | `mesh_shared`, `mesh_cmd`, `mesh_src`, `mesh_peer_event`, `mesh_job_event`, `mesh_actual`, `mesh_shared_ask/change/http/send_*` |
| `control.rs` | `mesh-control` | `mesh_control_handle/serve/client`, `mesh_control_programs/pipes/cluster/view/pipes_recs` |
| `http.rs` | `mesh-http-relay` | `mesh_http_serve/client`, `mesh_http_view_route`, `mesh_http_render_route` |
| `views.rs` | `mesh-views` | `mesh_rec_*`, `mesh_views_text/json/parse/format_of/render` |
| `net.rs` (counters) | `mesh-net-counters` | `mesh_net_counters_*` |
| `net.rs` (interfaces) | `mesh-nic` | `mesh_nic_*`, `mesh_nics_*` |
| `net.rs` (`sampler`), `load.rs` (`sampler`) | `mesh-samplers` | `mesh_net_sampler`, `mesh_load_sampler`, hook `mesh_load_perf` |
| `load.rs` (the /proc parsers) | `mesh-load-proc` | `mesh_parse_proc_stat`, `mesh_classify`, `mesh_class_util`, `mesh_parse_psi`, `mesh_psi_rate`, `mesh_parse_meminfo`, `mesh_parse_loadavg`, `mesh_proc_field` |
| `load.rs` (`Nl`, `Share`, `quantile`) | `mesh-load-share` | `mesh_nl_*`, `mesh_share_of`, `mesh_quantile` |
| `load.rs` (`Host`, OS readers, `derive`, `machine_bytes`, `exec_totals`, `loadrep`) | `mesh-load-host` | `mesh_host_read`, `mesh_host_derive`, `mesh_machine_bytes`, `mesh_exec_totals`, `mesh_loadrep` |
| `load.rs` (`Prog`) | `mesh-load-prog` | `mesh_prog_sample`, `mesh_nl_read` |
| `load.rs` (`text`) | `mesh-load-text` | `mesh_load_text` |
| `infer.rs` (frames, `Msg`, layers) | `mesh-infer-wire` | `mesh_infer_*_frame`, `mesh_infer_msg_*`, `mesh_infer_layer_*` |
| `infer.rs` (`Request`, `parse`, `from_query`, `text`) | `mesh-infer-request` | `mesh_infer_parse`, `mesh_infer_from_query`, `mesh_infer_text` |
| `infer.rs` (`Svc`) | `mesh-infer-svc` | `mesh_infer_svc_*` |
| `inferd.rs` | `mesh-inferd` | `mesh_inferd_run`, `mesh_inferd_members_of` |
| `args.rs` | `mesh-args` | `mesh_args_parse`, `mesh_args_backend_of`, `MESH_ARGS_USAGE` |
| `main.rs` (the fleet member's start), `peer.rs` (`run`'s start, `writer`, the reader's direct frames, `catalog`, `serve_image`, `send_view`), `reconcile.rs` (`run`'s driver calls), `load.rs` (`perf_report`) | `mesh-fleetd` | `mesh_fleetd_open/caps/start/stop/free` (the relay's `mpr_host`, the reconciler's `mrc_ops`, the bridges, `mesh_load_perf`) |
| `driver.rs` (placement: `PSlot`, `sync_place`, `place_acts`, `apply_mode`, `place_tick`, `place_frame`, `placed_here`, `place_report`) | `mesh-driver` (`src/place.c`) | `drv_sync_place`, `drv_place_*`, `drv_cmd_place`, `drv_cmd_peer_*`, `drv_cmd_fleet_view` (internal) |
| `main.rs` | `apps/meshd` | `meshd_main` |
| Rust's `String`/`format!`, `mpsc`, reply senders | `mesh-buf`, `mesh-rsfmt`, `mesh-chan`, `mesh-reply` | building blocks |

## How it is verified

- **The Rust's unit tests**, ported one for one: args (4), views (1), net (7), load (8), status (1),
  programs (4), publish (3), driver (2), control (1), http (1), infer (3).
- **A fleet of three whole meshds** (apps/meshd's `tests/cluster.c`, `--backend wasm` on loopback, one
  cluster config): they elect one leader and agree on it, the term and the committed version; `load`
  and `start` asked of a follower commit through the leader and every member reconciles them (the
  image from its own catalog); `job submit hashjob items 240` on a follower runs on the three members'
  wasm lanes (188 / 27 / 25 items in one run) and `job results` holds every item exactly once, each
  result and job_out recomputed in the test with mesh-ref's sha256d; `jobs` answers on every member and
  the HTTP relay answers; a member killed with SIGKILL is seen `down` by the two left, which keep their
  leader and commit a change (`stop hashjob`: the dead member `absent: reconcile on join`); the two exit
  0 on SIGTERM. About 25 s.
- **Placement in the driver** (mesh-driver's `tests/place.c`): a peered driver over the fake executor
  with a placeable program: the report, a pin asked of the leader, a refusal as a follower, the
  leader's PL_SET pinning it elsewhere (state written into the view, its kernel stopped, start and
  reset refused with the Rust's reason, input forwarded as F_INPUT, PL_DONE back, the inventory still
  counting it run), back to local, unload.
- **Golden text from the Rust**: Rust's formatting rules (`{:?}` of strings and durations, float
  `Display`, `from_utf8_lossy`, number parsing) were checked against rustc 1.88's output.
- **End to end**: the driver over a fake executor (mesh-driver's test); the control port and the HTTP
  relay over TCP; and a whole CPU-only meshd (`--backend wasm`) running the real hello wasm program
  and a native worker beside it (apps/meshd's test): autoload, tasks, stop/start, reset blinks,
  unload/load, peek, the views, a native crash failing only its program, `--state`, SIGINT, exit 0.
- **Without a GPU**, meshd's default backend fails exactly as the Rust's does: nvJitLink first, exit 1.
- Every package builds and tests with gcc, and with clang under `--profile asan` (leak-free): on the
  merged tree (main + this area), `sexc test --timeout 900` and `CC=clang sexc test --profile asan
  --timeout 1800` each pass 351 of 351 test programs, the three-meshd fleet included.

## What meshd does today (no GPU)

`meshd --backend wasm --programs DIR [--autoload] [--state FILE]` is a working CPU-only meshd: wasm
programs on lease lanes and native worker processes on one shared mailbox, every lifecycle operation,
the control port (every request), the HTTP port (the views, the relay to an http program, infer), the
load and NIC telemetry, `--state`, signals and the watchdog, and the job table (`job submit`, `jobs`,
`job status`, `job results`; payloads in the blob store) with its own leader.

With `--cluster FILE --system K --fleet-state FILE` (or `--peer` / `--peer-listen` for a pair) it is a
fleet member: links to every member, the election and the committed desired state, changes asked of
any member committed by the leader and reconciled on every member (images from the local catalog or
fetched from a member), the scheduler's model and texts (`fleet`, `groups`, `sched`, `view fleet`),
F_LOADREP and F_PERF every second, jobs leased across the members' executors and committed by a
majority, placement of a placeable program (pin / split) coordinated by the leader, and the lockstep
data plane. With a GPU, the CUDA backend (blink or `--isolation mps`) is exec's and is wired the way
docs/port/exec.md says.

## What remains

- **gpt2x.rs** (the host side of programs/gpt2x, on exec's mesh-inproc): runs only on a GPU; not ported.
- **Not exercised without a GPU**: placement end to end (the only placeable program, doom, is a GPU
  program; the driver's placement is tested over a fake executor, the placer in mesh-placer), a
  lockstep group across members (train is GPU; the relay's data plane is the fleet area's, tested in
  pieces); a migration's view transfer between real meshds is not driven by a test either.
- **One process, one fleet member**: `mesh_load_perf` is a process-wide hook, so mesh-fleetd keeps
  its member in a static (meshd has one).
- **The relay's own changes**: `mpr_board_rtts` was added to @ajax/mesh-peer-relay (0.1.1) for the
  F_PERF's link RTTs; nothing else of the fleet's, jobs' or exec's packages changed.
- meshd exits without stopping the fleet, jobs and blob threads, as the Rust's `log::exit` does.

## Notes for the integrator

- Two packages were both `@ajax/mesh-text` with header `mesh/text.h`: exec's (`mesh_text_*`) keeps the
  name; the fleet's (`mt_*`, used by the fleet, jobs and top packages) is `@ajax/mesh-mt` with
  `mesh/mt.h` on this branch. Every later merge of a branch using `mt_*` needs its includes switched
  (`sed 's|"mesh/text.h"|"mesh/mt.h"|'` on files using `mt_`, and the dependency renamed).
- String and parsing helpers exist several times over (mesh-buf/mesh-rsfmt here, mesh-text in exec,
  mesh-mt/rust-fmt/mesh-parse in fleet, mesh-strbuf/mesh-rs-parse/mesh-debug-str in jobs): worth
  unifying before publishing.
- sexc compiles and links a package's tests **without the package's own `cflags`/`ldflags`**; tests
  here define `_XOPEN_SOURCE` themselves and avoid libm. Reported for the sexc repo.
