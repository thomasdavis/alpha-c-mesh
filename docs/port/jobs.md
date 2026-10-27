# Port: job arrays across the fleet (area `jobs`)

This area holds meshd's job arrays, in 31 packages:

- the replicated job table (its ops, log, merge and replicas);
- the node, which is the leader's dispatcher and a member's side of the leases;
- payload results and the blob store;
- the image assembler;
- the jobs and blob threads that run them inside meshd.

README "Jobs: job arrays across the fleet" and "Review fixes (F7)" are the behavioural spec.

Pure logic is kept apart from I/O. Everything up to `mesh-jobs-node`, `mesh-blobd`'s logic and
`mesh-assemble` is deterministic and has no threads:

- The node returns its outputs (`mj_outs`) and never sends anything itself.
- The blob thread's logic sees the world through `mbd_io`.

Only `mesh-blobs-durable`, `mesh-blobs-store`, `mesh-jobd-store` and `mesh-jobd` touch files, and
only `mesh-jobd` runs threads.

## Rust → C

| Rust (crates/mesh-host/src) | C package | C symbols |
|---|---|---|
| `jobs.rs` `Ranges` | `mesh-jobs-ranges` | `mj_ranges_*` |
| `jobs.rs` `JobId`, `Hash`, `job_word`, `parse_job`, `hex` | `mesh-jobs-id` | `mj_job_word`, `mj_parse_job`, `mj_hex`, `mj_unhex` |
| `jobs.rs` `W`/`R` (the binary encoding), lossy UTF-8 | `mesh-jobs-wire` | `mj_w_*`, `mj_rd`, `mj_utf8_lossy` |
| `jobs.rs` `JobSpec`, `Mode` | `mesh-jobs-spec` | `mj_spec`, `mj_spec_encode/decode`, `mj_mode_*` |
| `jobs.rs` `Op`, `Res`, `BlobRef`, `encode_ops`, `parts` | `mesh-jobs-op` | `mj_op`, `mj_put_op/mj_get_op`, `mj_parts_of` |
| `jobs.rs` `Job`, `Table` (apply, ops, next id, states) | `mesh-jobs-table` | `mj_job_*`, `mj_table_*` |
| `jobs.rs` `lid_term`, `lid_next`, lease ids | `mesh-jobs-lid` (header-only) | `mj_lid_term`, `mj_lid_next`, `mj_lease_base` |
| `jobs.rs` `Frame`, `Prog`, `Held`, `LeaseMsg`, `ItemRes` | `mesh-jobs-frame` | `mj_frame`, `mj_frame_encode/decode` |
| `jobs.rs` `Req`, `Submit`, `SUBMIT_USAGE` | `mesh-jobs-req` | `mj_req_parse`, `mj_submit_parse` |
| `jobs.rs` `Log`, `Entry`, `Replica`, `replay` | `mesh-jobs-log` | `mj_log`, `mj_replica_*`, `mj_replay` |
| `jobs.rs` `copies_for` | `mesh-jobs-copies` (header-only) | `mj_copies_for` |
| `jobs.rs` `Rate`, `lease_time` | `mesh-jobs-rate` | `mj_rate_*`, `mj_lease_time` |
| `jobs.rs` `Out`, `Disk`, `durable` | `mesh-jobs-out` | `mj_out`, `mj_disk`, `mj_durable` |
| `jobs.rs` `Answer`, `merge` | `mesh-jobs-merge` | `mj_answer`, `mj_merge` |
| `jobs.rs` `select`, `Pick`, `SchedCx` (the scheduler half) | `mesh-jobs-select` | `mj_select`, `mj_picks`, `mj_sched` (interface) |
| `jobs.rs` `SchedCx` judged by `sched::filter`/`score_terms` | `mesh-jobs-sched` | `mjs_cx`, `mjs_sched` |
| `jobs.rs` `Node` (replica, lead, dispatch, staging, frames) | `mesh-jobs-node` | `mj_node_*` (src: node.c, frames.c, lead.c, dispatch.c) |
| `blobs.rs` `make_durable`, `write_durable`, `sync_dir` | `mesh-blobs-durable` | `mb_make_durable`, `mb_write_durable`, `mb_sync_dir` |
| `blobs.rs` `Store` | `mesh-blobs-store` | `mb_open`, `mb_reserve`, `mb_written`, `mb_get`, `mb_drop_job`, … |
| `blobd.rs` the F_BLOB_* frame layouts | `mesh-blobs-frame` | `mbf_put/ack/get/blob`, `mbf_decode` |
| `blobd.rs` `Blobd` (events, pushes, fetches, assemblies, render) | `mesh-blobd` | `mbd_*`, `mbd_io` |
| `assemble.rs` | `mesh-assemble` | `masm_*`, `masm_part_of`, `masm_renders` |
| `assemble.rs` `bmp` | `mesh-assemble-bmp` | `masm_bmp` |
| `jobd.rs` `Store` | `mesh-jobd-store` | `mjd_store_*` |
| `jobd.rs` `jobs_text`, `job_line`, `status_text`, `results_text` | `mesh-jobd-text` | `mjd_jobs_text`, `mjd_status_text`, `mjd_results_text` |
| `jobd.rs` `run`, `apply`, `ctl`, `publish`, `news`, `jobs_recs`; `blobd.rs` `run`, `writer`, `render`; shared.rs `jobs_view`, `jobs_sched`, `render_watch` | `mesh-jobd` | `mjd_open/start/stop/free`, `mjd_set_view`, `mjd_jobs_sched`, `mjd_jobs_frame`, `mjd_blobs_frame`, `mjd_render` |
| `BTreeMap` / `BTreeSet` | `mesh-omap` | `momap_*` (AVL tree, 1–6 word keys, O(1) cursor) |
| `String` / `format!` | `mesh-strbuf` | `msb_*`, `mfmt` |
| `{:?}` of a string | `mesh-debug-str` | `mdebug` |
| `str::parse` / `from_str_radix` | `mesh-rs-parse` | `rs_parse_uint`, `rs_u64`, … |

`mesh-jobs-sim` is private and is never published. It is the test bed: the Rust tests' `Sim`
(`tests/fsim.h`), the scripted trace harness and the fleet tests.

## Package graph

Dependencies come first. Each line depends only on lines above it (and on `mesh-check`):

```
mesh-strbuf, mesh-omap, mesh-debug-str, mesh-rs-parse, mesh-ref-sha256d (REF)
mesh-jobs-ranges, mesh-jobs-id, mesh-jobs-wire
mesh-jobs-spec → mesh-jobs-op → mesh-jobs-table
mesh-jobs-lid, mesh-jobs-frame (op, table), mesh-jobs-req (spec)
mesh-jobs-log (table, frame), mesh-jobs-copies, mesh-jobs-rate
mesh-jobs-out (log, frame), mesh-jobs-merge (log), mesh-jobs-select (frame, spec)
mesh-jobs-sched (select + FLEET's mesh-sched-member/-needs/-score/-weights)
mesh-jobs-node (all of the above but sched)
mesh-blobs-durable → mesh-blobs-store (id), mesh-blobs-frame
mesh-assemble (pathtrace-* from REF, mesh-ftz), mesh-assemble-bmp
mesh-blobd (store, frame, assemble)
mesh-jobd-store (log, out), mesh-jobd-text (node)
mesh-jobd (node, jobd-store, jobd-text, blobd, jobs-sched + DAEMON's mesh-shared, mesh-chan,
           mesh-reply, mesh-views, mesh-host-block, mesh-log + FLEET's mesh-fleet-doc)
```

Every package has a README and full `sexc.json` metadata (MIT, the repository, keywords). Each one
that needs feature macros sets `-D_XOPEN_SOURCE=700` (and `-pthread`) in its own manifest.

## What DAEMON wires

All of it is in `@ajax/mesh-jobd`; see its README for the exact calls.

1. **Start-up.** `meshd.c` currently calls `mesh_chan_close_rx(sh->jobs)`. Replace that with the
   following, in this order:
   1. `mjd_open(sh, &(mjd_config){a.jobs_state, a.blobs, a.blobs_cap_mb << 20, dead_ms}, &err)`
      right after `mesh_shared_new`. It replays the job log and indexes the blob store before
      anything runs. On failure, `fail(err)`.
   2. `mesh_control_render = mjd_render`.
   3. `mjd_start` once the driver has started (Rust starts these threads after the control, http and
      sampler threads).

   `dead_ms` is the cluster's `tun.dead_ms`, or `MESH_PEER_DEAD_MS` when single.
2. **The fleet thread** (`mesh-peer-relay`'s `mpr_host`), one-line wrappers with `sh`:
   - `jobs_view` → `mjd_set_view`
   - `jobs_sched` → `mjd_jobs_sched`
   - `jobs_frame` → `mjd_jobs_frame`
   - `blobs_frame` → `mjd_blobs_frame`
   - `jobs_ctl` → a `MESH_JEV_CTL` event on `sh->jobs`:
     - local: `reply_to` LOCAL, and a `mesh_reply` whose answer feeds the `mpr_reply`;
     - remote: `reply_to` REMOTE, with `remote_from` and `remote_id`. The jobs thread sends the
       F_CTL_REPLY frame itself, as jobd.rs does.
3. **Already in place on DAEMON's side.** These are used as they are:
   - the driver's `MESH_JEV_DONE` and its `MESH_CMD_JOB_LEASE/JOB_CUT/JOB_PROGS` handling;
   - control's `jobs`/`job` requests (`MESH_JEV_CTL`) and `view jobs` (`sh->jobs_view_recs`);
   - `sh->blob_mem` and `mesh_shared_fail_stop`.

The messages this area defines on DAEMON's channels:

- `MESH_JEV_BLOBS_STORED` and `MESH_JEV_BLOB_GONE`: `msg` is an `mb_refs *` (free with `msg_free`).
- `MESH_JEV_BLOB_ACKED`: `msg` is an `mb_ref *`, with `from` and `ok`.
- `sh->blobs` carries `mesh-jobd`'s own blob events (internal).
- `sh->jobs_ext` is `mesh-jobd`'s state: the view, the pending scheduler model and the watched images.

## Verification

79 test programs in this area, all of which pass with `sexc test` and
`CC=clang sexc test --profile asan`:

- 30 package tests;
- 49 in `mesh-jobs-sim`.

**Every Rust test ported.** Every `#[test]` in jobs.rs (32, including the `#[ignore]`d 400-seed
chaos run), blobs.rs (4), assemble.rs (2) and jobd.rs (1) has a C counterpart, and the C test names
the Rust test it ports:

- **Unit tests** (`ranges`, `copies`, `ops_frames_and_the_merge`, the review findings' node tests,
  `nothing_leaves_without_its_persist`, `select_is_the_schedulers_filter_and_score`,
  `a_snapshot_keeps_the_entries_acknowledged_past_it`, the blob store's and the assembler's tests,
  `a_torn_write_is_cut_back`) are in their packages.
- **The fleet tests** run on `mesh-jobs-sim`'s port of the Rust `Sim`, a whole fleet of nodes with a
  scripted network and clock. The slow ones are split into several programs: leader 31..47, chaos
  1..8, survive 61..71, full store 0..3 and assemble 0..23.
- The chaos test runs 40 seeds. `MESH_CHAOS_SEEDS=400` runs the ignored 400.

**Golden vectors from the Rust.** These were built with `cargo +1.88.0` on the pure modules (jobs.rs,
blobs.rs, assemble.rs, log.rs, …) copied into a scratch crate at `/mnt/donto-data/scratch/port-jobs/gold`.
The C and Rust sides use the same xorshift generator, and the tests compare FNV-1a hashes of the
outputs.

- **Encodings.**
  - ops, frames, log records and requests round-trip, and their bytes are hashed against the Rust's
    (`mesh-jobs-op`, `-frame`, `-log`, `-req`, `-spec`, `-wire`);
  - lossy UTF-8.
- **State transitions.**
  - the table's `apply` over random op sequences, with the resulting tables hashed and the Rust's
    `eq` flag recorded (`mesh-jobs-table`);
  - replica replay (`mesh-jobs-log`) and the merge of answers (`mesh-jobs-merge`);
  - `select` (`mesh-jobs-select`).
- **The node, output for output.** `mesh-jobs-sim/tests/trace.c` runs seven scripted fleet scenarios
  (submit, leases, results, leader change, pull and merge, payloads, fetch, cancel and forget)
  through the C node and the Rust node. Every output of every step is canonically encoded and
  hashed, and the C matches the Rust in all seven (`gold_trace.h`).
- **Images.** The assembled image's reference digests (`REF_ACC 0xff7f387eca86cb49`,
  `REF_RGB 0x6babf0f12f7483b0`) and the default-scene BMP match the Rust.

**Failure injection, in-process:**

- the sim's crashes, restarts, dropped and duplicated frames and clock jumps (chaos, survive, leader);
- torn log writes (`mjd_store_tear_next`) and failing disks (`mj_durable`'s fail-stop, `mesh-jobs-out`);
- corrupt blob files and pushes (`mesh-blobs-store`, `mesh-blobd`);
- a store that is full (`fleet_full_store_*`).

**Blob store.** Round trips through the writers on disk and in memory, a blob shared by two jobs
outliving either, eviction of only forgotten jobs, sha256 verified on read (a corrupt file is
dropped and reported lost), and leftover `.tmp`s removed at open.

**The threads.** `mesh-jobd/tests/jobd.c` drives the real jobs and blob threads over a `mesh_shared`
with a fake driver:

- **Single meshd.** A job with 64-byte payloads runs to the end, and every payload is durable in the
  store on disk. The test checks the host block's `S_JOB_*` fields, the jobs view's records, and the
  control answers and their exact error texts.
- **`render` errors**, **frame routing** and **backpressure** on `blob_mem`.
- **Restart.** The log is replayed (a forgotten job stays forgotten) and the store is indexed.
- **Follower.** A follower forwards a change to the fleet thread (`MESH_PEV_CHANGE`) and answers a
  remote request with F_CTL_REPLY.

## Gaps

- **meshd is not wired yet.** `apps/meshd` still closes `sh->jobs`. The wiring above is DAEMON's
  (or the lead's) to do; `mesh-jobd`'s test exercises the same calls in-process.
- **No two-meshd test.** The threads are tested single and as a follower with a fake fleet thread.
  A fleet of real meshds exchanging F_JOB_* and F_BLOB_* frames over `mesh-peer-relay` has not run
  here. The node and blobd logic is exercised fleet-wide in `mesh-jobs-sim` and in blobd's
  three-node test.
- **Loaded box.** Under a load average of 40–60 on 8 cores, the sim programs need
  `sexc test --timeout 600`.
- **No Metal.** Nothing in this area is GPU code.
- **Duplicated string helpers.** `mesh-strbuf` (`msb`, `mfmt`) duplicates EXEC's `mesh-text` and
  FLEET's `mesh-mt`. The names don't collide, but converging on one is an integration cleanup.
  `mesh-omap` (ordered maps) has no counterpart elsewhere.
