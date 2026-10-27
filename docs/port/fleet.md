# Port: the fleet (area `fleet`)

This area holds the fleet layer of meshd, in 32 packages:

- membership, the leader and the desired state (the Raft-like core);
- the scheduler (filter, score, place, rebalance, migrate);
- placement (splitting one program's work across members);
- the reconciler's decisions;
- the peer protocol: frames, links, view transfer;
- the fleet thread that ties it all together and publishes it.

Pure logic is kept apart from I/O. Everything up to `mesh-fleet-core`, the scheduler and the placer is
deterministic and has no threads. Only `mesh-fleet-store`, `mesh-peer-link`, `mesh-reconcile`
(`mrc_thread`) and `mesh-peer-relay` touch sockets, files or threads. The relay reaches the daemon
only through callbacks.

## Rust → C

All paths are under `crates/mesh-host/src`.

| Rust | C package | Main symbols |
|---|---|---|
| `fleet.rs` (`Doc`, `ProgSpec`, `Group`, `Op`, `encode`/`decode`, `can_run`) | `mesh-fleet-doc` | `mfd_doc`, `mfd_prog`, `mfd_group`, `mfd_encode`, `mfd_decode`, `mfd_can_run` |
| `fleet.rs` (`Persist`) | `mesh-fleet-persist` | `mfp_persist`, `mfp_encode`, `mfp_decode` |
| `fleet.rs` (`Core`: roles, PreVote, CheckQuorum, the fixed pair, catch-up, durable outputs) | `mesh-fleet-core` | `mfc_new`, `mfc_tick`, `mfc_recv`, `mfc_link`, `mfc_propose`, `mfc_durable`, … |
| `fleet.rs` (`new_epoch`, group policy, re-form) | `mesh-fleet-groups` | `mfg_new_epoch`, `mfg_policy`, `mfg_reform` |
| `fleet.rs` (`Req`, `edit`, `revert`) | `mesh-fleet-edit` | `mfe_parse`, `mfe_edit`, `mfe_revert`, `mfe_image`, `mfe_ctx` |
| `fleet.rs` (`plan`, `seed`, the reconcile actions) | `mesh-fleet-plan` | `mfl_plan`, `mfl_seed`, `mfl_actual`, `mfl_action` |
| `fleet.rs` (`members_of`, `mask_of`, lists) | `mesh-member-list` | `ml_list`, `ml_opt`, `ml_parse`, `ml_text`, `ml_at_word`, `ml_mask`, `ml_members` |
| `peer.rs` (`Store`) | `mesh-fleet-store` | `mfs_open`, `mfs_write` (fsync + rename), `mfs_close` |
| `sched.rs` (`Member`, `Perf`) | `mesh-sched-member` | `msm_member`, `msm_perf`, `msm_perfs`, `msm_exec_for` |
| `sched.rs` (`Term::matches`, the document's needs token, `summary`) | `mesh-sched-needs` (on EXEC's `mesh-manifest-needs`) | `msn_term_matches`, `msn_encode`, `msn_decode`, `msn_summary` |
| `sched.rs` (`Weights`) | `mesh-sched-weights` | `msw_weights`, `msw_default`, `msw_from_keys` |
| `sched.rs` (`Report`, F_PERF text) | `mesh-sched-report` | `msr_report`, `msr_encode`, `msr_decode` |
| `sched.rs` (`filter`, `eligible`, `Score`, `explain`) | `mesh-sched-score` | `msc_filter`, `msc_eligible`, `msc_score_terms`, `msc_score`, `msc_explain` |
| `sched.rs` (`place`, `Explain`, `order`, `group_choice`, `group_score`) | `mesh-sched-place` | `msp_place`, `msp_explain_text`, `msp_group_choice`, `msp_group_score` |
| `sched.rs` (`Tracker`: hysteresis, budget, dwell, owner policy; `Move`, `Outcome`, verdicts) | `mesh-sched-tracker` | `mst_new`, `mst_step`, `mst_group_step`, `mst_owner`, `mst_mig_verdict` |
| `sched_tests.rs` | tests of the `mesh-sched-*` packages | all 17 |
| `cluster.rs` | `mesh-cluster-conf` | `mcc_topology_parse`, `mcc_keys`, `mcc_tunables`, `mcc_pair_dialer`, `mcc_pair_listener` |
| `place.rs` (`Spec`) | `mesh-place-spec` | `mps_spec` |
| `place.rs` (modes, `assign`, `auto`) | `mesh-place-mode` | `mpm_mode`, `mpm_target`, `mpm_assign`, `mpm_even`, `mpm_auto`, … |
| `place.rs` (F_PLACE / F_FRAME / F_INPUT) | `mesh-place-wire` | `mpw_place`, `mpw_frame`, `mpw_bytes`, `mpw_name_of` |
| `place.rs` (`Composer`) | `mesh-place-compose` | `mpc_*` |
| `place.rs` (`Placer`: the change protocol) | `mesh-placer` | `mpl_new`, `mpl_request`, `mpl_view`, `mpl_tick`, `mpl_peer_place`, `mpl_own_frame` |
| `reconcile.rs` | `mesh-reconcile` | `mrc_ops`, `mrc_rec`, `mrc_document`, `mrc_retry`, `mrc_thread_start`, `mrc_cycle_ready`, `mrc_stop_order` |
| `peer.rs` (frames: JOIN, BEAT, VOTE, ACK, HELLO, caps) | `mesh-peer-codec` | `mpk_join`, `mpk_caps`, `mpk_beat_frame`, `mpk_msg_frame`, `mpk_hello` |
| `peer.rs` (`connect`, `handshake`, acceptor, dialer, reader, writer) | `mesh-peer-link` | `mpn_connect`, `mpn_handshake`, `mpn_link_start`, `mpn_send`, `mpn_acceptor_start`, `mpn_dialer_start` |
| `peer.rs` (view transfer: chunks, waits, load averages) | `mesh-peer-view` | `mpv_wait`, `mpv_take`, `mpv_load` |
| `peer.rs` (received ranges) | `range-set` | `rs_*` (also for jobs) |
| `peer.rs` (`run`, `Relay`, publish, texts), `peer_sched.rs`, `shared.rs` `PerfBoard` | `mesh-peer-relay` | `mpr_host`, `mpr_start`, `mpr_stop`, `mpr_change`, `mpr_fetch_image`, `mpr_fetch_view`, `mpr_board_own`, … |
| Rust's `{}` / `{:?}` / `{:#x}` where printf differs | `rust-fmt` | `rf_f32`, `rf_f64`, `rf_debug_str`, `rf_hex_alt` |
| str helpers | `mesh-mt` (DAEMON has the same copy) | `mt_buf`, `mt_strs`, `mt_fmt`, `mt_split`, … |
| `str::parse` | `mesh-parse` | `mp_u32`, `mp_u64`, `mp_f32`, `mp_f64`, … |
| `BTreeMap<String, String>` | `mesh-strmap` | `ms_map` |
| (golden vectors) | `mesh-fleet-golden` (private) | Rust's outputs as data, compared byte for byte |

## Package graph

Each layer uses only the layers below it (and the `mesh-abi-*` contract).

1. **Foundations:** `rust-fmt`, `mesh-mt`, `mesh-parse`, `mesh-strmap`, `range-set`,
   `mesh-member-list`.
2. **Scheduler data:** `mesh-sched-member`, `mesh-sched-weights` and `mesh-sched-report`, plus
   `mesh-sched-needs`, which sits on EXEC's `mesh-manifest-needs`.
3. **Documents and decisions:**
   - `mesh-fleet-doc` → `mesh-fleet-persist` → `mesh-fleet-core`;
   - `mesh-fleet-groups`, `mesh-fleet-plan`, `mesh-fleet-edit`;
   - `mesh-sched-score` → `mesh-sched-place` → `mesh-sched-tracker`;
   - `mesh-cluster-conf`.
4. **Placement:** `mesh-place-spec`, `mesh-place-mode` → `mesh-place-wire` → `mesh-place-compose` →
   `mesh-placer`.
5. **I/O:** `mesh-fleet-store`, `mesh-peer-codec` → `mesh-peer-link`, `mesh-peer-view`,
   `mesh-reconcile`.
6. **The fleet thread:** `mesh-peer-relay`.

## What DAEMON wires

- **`mesh-peer-relay`:** fill an `mpr_host`. Every callback is required except `jobs_sched`.
  - The status block: `host_u32`, `host_u64`, `host_str`, `host_bytes`, `host_read`, `uptime_ms`.
  - The mailbox: `mb_*`, `lock_feed`, `round_stats`.
  - The reconciler: `committed` (post the document to `mrc_thread`) and `recon` (its last status, as
    `mrc_ops.status` reported it).
  - The catalog: `catalog`, which builds an `mfe_image` from the manifest. Its `needs` is the manifest's
    `mesh_needs` (copied) and its `backends` is the manifest's backends.
  - Images and views: `inventory`, `image`, `stop_and_view`.
  - The driver: `driver_view`.
  - Frames for others: `place_frame` (to the `mesh-placer` instances), `jobs_frame`, `blobs_frame`.
  - Jobs: `jobs_ctl`, `jobs_view`, `jobs_sched`.
  - Texts for the control port and the views: `texts`, `sched_texts`.
  - The rest: `peer_state`, `net_count`, `log`, `fail_stop`.

  Then call `mpr_start` with `mcc_topology_parse` of `--cluster`, the machine's `mpk_caps` and
  `--fleet-state`. The README lists which callbacks run on which thread.
- **Control port:** `mpr_change(line, reply)` for every change line. `mpr_wake` when the reconciler's
  status changed. `mpr_group_want` and `mpr_group_state` for the lockstep group.
- **The perf board:** `mpr_board_own` with this member's `msr_report` each period;
  `mpr_board_echoes` to fill the next F_PERF's echoes.
- **`mesh-reconcile`:** fill `mrc_ops` with the driver's actions (`source`, `load`, `upgrade`,
  `unload`, `rollback`, `prog_run`, `reset`, `task`, `place`, `view_put`). Its `fetch_view` is
  `mpr_fetch_view`. Its group callbacks are `mpr_group_want`. Its `status` and `fails` feed the
  relay's `recon`.
- **`mesh-placer`:** one `mpl_placer` per placeable program, with its `mps_spec` built from the
  manifest's `place` keys. The relay hands it F_PLACE, F_FRAME and F_INPUT frames.
- **Jobs:** `range-set` for the ranges it tracks. `jobs_sched` gives the scheduler's model (members,
  committed document, perf, weights) for `jobs::select`.

## Verification

- **Every Rust test ported: 55 of 55.**
  - `fleet.rs`: 17, including the `#[ignore]`d long randomised run, which is `election N`.
  - `sched_tests.rs`: 17.
  - `place.rs`: 12.
  - `peer.rs`: 6.
  - `cluster.rs`: 2.
  - `reconcile.rs`: 1.

  Each C test names its Rust test in a comment, so `grep -rw <rust_test_name> packages/*/tests` finds
  it.
- **Golden vectors.** A scratch Rust crate, built with cargo 1.88.0 against the Rust sources, wrote
  inputs (floats as bits) and outputs (as length and FNV-1a 64). `mesh-fleet-golden` replays them, and
  all match byte for byte:
  - `floats.txt`: f32/f64 Display;
  - `needs.txt`: the document token and the summary;
  - `sched.txt`: 150 random scheduler scenarios covering filter, score, explain, place, tracker steps,
    owner policy and group choice;
  - `sim.txt`: 80 seeds of the fleet core's simulated network (partitions, crashes, restarts, loss). Every leadership, committed version (with its document's hash), fail-stop and final (term, role, version) matches the Rust's run.
- **Loopback simulation** (`mesh-peer-relay/tests/loopback.c`). Three relays run over real TCP, each
  with a fake daemon, and the test checks that:
  - they elect a leader;
  - a change on a follower is forwarded, committed, reconciled everywhere and answered;
  - a refused change carries its reason;
  - a stopped member is detected (fleet core, status block, `fleet` text);
  - the majority keeps committing;
  - the member comes back and catches up;
  - a stopped leader is replaced;
  - a lone member refuses changes.

  It takes about 2.5 s and was stable over 10 runs with gcc and 10 under ASan.
- `sexc test` and `CC=clang sexc test --profile asan` pass on the whole workspace, merged with `main`:
  180 test programs, 34 of them this area's.

## Gaps

- The relay's lockstep data plane (HELLO, rounds), image fetch and migration view transfer compile
  and are ported line by line. Their pure pieces are tested (`mesh-peer-view`, `mesh-peer-codec`,
  `mesh-sched-tracker`'s verdicts), but the loopback simulation does not drive them end to end. That
  needs a driver with programs in the mailbox, which is DAEMON's integration.
- The relay's published texts (`fleet`, `groups`, `sched`, the view records) follow the Rust
  format strings but have no golden vectors. The pure functions under them do.
- The fleet view's JSON form (views.rs) is not here; the relay produces the text form.
- Merges: `port-daemon` and `port-jobs` carry an older copy of `mesh-sched-needs` with its own
  `msn_needs` and `msn_term` types. This branch replaces them with EXEC's `mesh_needs` and `mesh_term`.
  Code there that uses the old fields needs the same mechanical change:
  - `.backends.v` / `.n` → `.backends` / `.nbackends`, and likewise for `prefer`, `avoid`, `feeds`
    and `group_members`;
  - `MSN_*` → `MESH_TERM_*` / `MESH_MIGRATE_*`;
  - `msn_eq` / `msn_free` → `mesh_needs_eq` / `mesh_needs_free`.
