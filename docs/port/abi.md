# Port: crates/mesh-abi

alpha-rust-mesh's `crates/mesh-abi` (the memory, control, snapshot and peer contract) is 59 C packages under
`packages/`: one per region or protocol of the contract, a few generic helpers, the umbrella `@ajax/mesh-abi`, and two
private device build checks.

## How it was ported, and how it is checked

- **Constants (876).** Every `pub const` of `v2.rs`, `v2/pipe.rs`, `v2/views.rs` and `v2/wasm.rs` was translated
  mechanically, its Rust doc comment and expression included (`MESH_PX_SHARED` is `(MESH_PX_FIELDS + MESH_FIELD_BYTES * MESH_MAX_FIELDS)`
  as in the Rust), and placed in the package that owns its region. Each package's `tests/constants.c` checks every
  constant against the value a Rust program printed from the crate itself.
- **Functions.** Hand-ported, `static inline` where the Rust is a `const fn` or generic over `Words`. Each package's
  `tests/golden.c` checks them against 1034 vectors the Rust computed on the same inputs.
- **The Rust's tests (10)** are ported one for one: 6 ring tests, 2 pipe tests, the pipedemo test, the layout test.
- **The Rust's const asserts** are `_Static_assert`s (in the owning package's header, or in `mesh/abi.h` when they span
  packages) where C can evaluate them, and runtime checks in tests where they call functions.
- **Device code.** Every header is freestanding and compiles for nvptx64 and wasm32; @ajax/mesh-abi-ptx-check and
  @ajax/mesh-abi-wasm-check build the whole contract into a PTX kernel and a wasm module.

## Naming

| Rust | C |
|---|---|
| `v2::NAME` | `MESH_NAME` |
| `v2::m::NAME` (pipe, wasm, and each `views::m`) | `MESH_M_NAME`, or `MESH_NAME` when `NAME` already starts with `M_` (`pipe::PIPE_OK` is `MESH_PIPE_OK`) |
| a `const` array `ARR: [T; N]` | `extern const T MESH_ARR[N]` in the package's `src/`, and `MESH_ARR_LEN` |
| `fn f` / `m::f` | `mesh_f` / `mesh_m_f` |
| `usize` offsets | integer constants; functions return `size_t` |
| `u32` / `u64` / `f32` constants | `u` suffix / `UINT64_C` / `f` suffix |
| `&[u8]` arguments | `(const uint8_t *, size_t)` |
| `Option<T>` | `bool` plus an out parameter (`skip_dead`: 1 / 0 / -1) |
| `Result<bool, PipeErr>` | `int`: 1, 0, or `-MESH_PIPE_ERR_*` |

## Functions and tests

| Rust file | Rust items | C package | C |
|---|---|---|---|
| `v2.rs` | backend_word, backend_kind, backend_family | mesh-abi-backend | mesh_backend_word, mesh_backend_kind, mesh_backend_family |
| `v2.rs` | prog_entry | mesh-abi-program-table | mesh_prog_entry |
| `v2.rs` | prog_ctl, run_req_next | mesh-abi-control-block | mesh_prog_ctl, mesh_run_req_next |
| `v2.rs` | node_rec | mesh-abi-node | mesh_node_rec |
| `v2.rs` | node_load | mesh-abi-node-load | mesh_node_load |
| `v2.rs` | trace_rec | mesh-abi-trace | mesh_trace_rec |
| `v2.rs` | ex_counter, ex_slots | mesh-abi-exchange | mesh_ex_counter, mesh_ex_slots |
| `v2.rs` | prog_ext | mesh-abi-ext | mesh_prog_ext |
| `v2.rs` | load_page | mesh-abi-load-page | mesh_load_page |
| `v2.rs` | pipe_entry, flight_word | mesh-abi-pipe-table | mesh_pipe_entry, mesh_flight_word |
| `v2.rs` | section_bytes | mesh-abi-snapshot | mesh_section_bytes |
| `v2.rs` | member | mesh-abi-host-members | mesh_member |
| `v2.rs` | link, frame_kind | mesh-abi-host-links | mesh_link, mesh_frame_kind |
| `v2.rs` | nic, nic_virtual | mesh-abi-host-nics | mesh_nic, mesh_nic_virtual |
| `v2.rs` | mload | mesh-abi-host-mload | mesh_mload |
| `v2.rs` | fmember | mesh-abi-host-fleet | mesh_fmember |
| `v2.rs` | placed | mesh-abi-host-sched | mesh_placed |
| `v2.rs` | pload | mesh-abi-host-pload | mesh_pload |
| `v2.rs` | show_bytes | mesh-abi-placement | mesh_show_bytes |
| `v2.rs` | fnv1a64 | mesh-abi-fnv | mesh_fnv1a64 |
| `v2.rs` | valid_name | mesh-abi-name | mesh_valid_name |
| `v2.rs` | disjoint, fields_end, the (usize, usize) tables | mesh-abi-fields | mesh_disjoint, mesh_fields_end, mesh_field |
| `v2.rs` | const _ asserts | each package (local), mesh-abi (spanning) | _Static_assert in headers; runtime CHECKs in tests |
| `v2.rs` | layout_tests | mesh-abi-fields, mesh-abi | tests/fields.c, tests/layout.c |
| `v2/pipe.rs` | Words, Bytes | mesh-abi-words | mesh_words (a function table), mesh_words_load/store/cas/add/fence/read/write |
| `v2/pipe.rs` | sum_part, rec_sum, pos_mix, rec_sum_at, type_hash | mesh-abi-pipe-hash | mesh_pipe_sum_part, mesh_pipe_rec_sum, mesh_pipe_pos_mix, mesh_pipe_rec_sum_at, mesh_pipe_type_hash |
| `v2/pipe.rs` | owner_tag, inbox_sums, inbox_data, ring_bytes, inbox_bytes | mesh-abi-pipe-ring | mesh_pipe_owner_tag, mesh_pipe_inbox_sums, mesh_pipe_inbox_data, mesh_pipe_ring_bytes, mesh_pipe_inbox_bytes |
| `v2/pipe.rs` | Ring (empty, full, lap, sum, cell, valid, reserve_push, commit_push, reserve_pop, take, commit_pop, abandoned, skip_dead, len, free) | mesh-abi-pipe-ring | mesh_pipe_ring, mesh_pipe_ring_<method> |
| `v2/pipe.rs` | Inbox (side, valid, first, take, len) | mesh-abi-pipe-ring | mesh_pipe_inbox, mesh_pipe_inbox_<method> |
| `v2/pipe.rs` | PipeErr, code, read_block, target, flight_begin, flight_end, push, pop | mesh-abi-pipe | mesh_pipe_err, mesh_pipe_code, mesh_pipe_read_block, mesh_pipe_target, mesh_pipe_flight_begin, mesh_pipe_flight_end, mesh_pipe_push, mesh_pipe_pop |
| `v2/pipe.rs` | tests (8) | mesh-abi-pipe-ring, mesh-abi-pipe | tests/ring.c (6), tests/pipe.c (2) |
| `v2/views.rs` | doom::show_data, doom::map_cell | mesh-abi-view-doom | mesh_doom_show_data, mesh_doom_map_cell |
| `v2/views.rs` | pipedemo::payload, msg, sha256d_msg, digest | mesh-abi-view-pipedemo | mesh_pipedemo_payload, mesh_pipedemo_msg, mesh_pipedemo_sha256d_msg, mesh_pipedemo_digest |
| `v2/views.rs` | pipedemo::compress (and K, H0) | sha256-compress | sha256_compress, SHA256_H0 |
| `v2/views.rs` | pipedemo::quantile | hist-quantile | hist_quantile |
| `v2/views.rs` | pipedemo_tests | mesh-abi-view-pipedemo | tests/pipedemo.c |
| `v2/wasm.rs` | w_peer | mesh-abi-wasm | mesh_wasm_w_peer |

## Constants by package

| Package | Header | Rust constants | Depends on |
|---|---|---|---|
| @ajax/mesh-abi-base | `mesh/abi/base.h` | `MAGIC`, `NODE_THREADS`, `MAX_WARPS`, `RT_SHARED_BYTES`, `MAX_SHARED_BYTES`, `SHARED_ALIGN` … (16) | — |
| @ajax/mesh-abi-control-protocol | `mesh/abi/control_protocol.h` | `CONTROL_PORT`, `HTTP_PORT`, `LOAD_REASONS` | — |
| @ajax/mesh-abi-backend | `mesh/abi/backend.h` | `BACKEND_CUDA`, `BACKEND_METAL`, `BACKEND_WASM`, `BACKEND_NATIVE`, `BACKENDS` | — |
| @ajax/mesh-abi-header | `mesh/abi/header.h` | `H_MAGIC`, `H_VERSION`, `H_PEER`, `H_BLINK`, `H_EPOCH`, `H_PROG_SEQ` … (21) | mesh-abi-fields |
| @ajax/mesh-abi-program-table | `mesh/abi/program_table.h` | `PROG_TABLE`, `PT_BYTES`, `PT_STATE`, `PT_GEN`, `PT_NAME`, `PT_VIEW_OFF` … (49) | mesh-abi-base |
| @ajax/mesh-abi-control-block | `mesh/abi/control_block.h` | `PROG_CTL`, `PROG_CTL_BYTES`, `LINE`, `P_SHUTDOWN`, `SHUTDOWN_HALT`, `SHUTDOWN_EXIT` … (32) | mesh-abi-fields |
| @ajax/mesh-abi-ring | `mesh/abi/ring.h` | `P_RING`, `PROG_RING`, `ENTRY_BYTES`, `E_OP`, `E_ARG`, `E_SLOT` … (10) | — |
| @ajax/mesh-abi-ops | `mesh/abi/ops.h` | `OP_TASK`, `OP_INIT`, `OP_REQ`, `OP_FOLLOW`, `OP_JOB`, `R_JOB_FAIL` … (8) | — |
| @ajax/mesh-abi-fault | `mesh/abi/fault.h` | `FAULT_VIEW_BOUNDS`, `FAULT_PEER_VIEW`, `FAULT_RESP_BOUNDS`, `FAULT_GPU`, `FAULT_TRAP`, `FAULT_TIME` … (8) | — |
| @ajax/mesh-abi-node | `mesh/abi/node.h` | `PROG_NODES`, `PROG_MAX_NODES`, `NODE_STRIDE`, `PROG_NODES_BYTES`, `N_SM`, `N_HEARTBEAT` … (10) | mesh-abi-fields |
| @ajax/mesh-abi-node-load | `mesh/abi/node_load.h` | `NODE_LOAD`, `NL_STRIDE`, `NL_OBS_NS`, `NL_LOST_NS`, `NL_GAPS`, `NL_MAX_GAP_NS` … (10) | mesh-abi-node, mesh-abi-fields |
| @ajax/mesh-abi-trace | `mesh/abi/trace.h` | `TRACE_BASE`, `TRACE_PER_PROG`, `TRACE_BYTES` | — |
| @ajax/mesh-abi-exchange | `mesh/abi/exchange.h` | `EX_BASE`, `EX_BYTES`, `EX_SLOT_BYTES`, `EX_REGION_BYTES`, `PEER_POLLS`, `PEER_POLL_NS` | — |
| @ajax/mesh-abi-ext | `mesh/abi/ext.h` | `PROG_EXT`, `PX_BYTES`, `PX_COUNTER_NAMES`, `PX_FIELD_COUNT`, `PX_PLACE_CAP`, `PX_PLACE` … (31) | mesh-abi-base, mesh-abi-control-block, mesh-abi-placement, mesh-abi-fields |
| @ajax/mesh-abi-load-page | `mesh/abi/load_page.h` | `LOAD_PAGE` | mesh-abi-host-load |
| @ajax/mesh-abi-pipe-table | `mesh/abi/pipe_table.h` | `PIPE_TABLE`, `PP_BYTES`, `PP_IN_OFF`, `PP_IN_CAP`, `PP_IN_REC`, `PP_IN_TYPE` … (13) | mesh-abi-base, mesh-abi-fields |
| @ajax/mesh-abi-view-heap | `mesh/abi/view_heap.h` | `VIEW_HEAP`, `VIEW_HEAP_END`, `VIEW_PAGE`, `VIEW_PAGES` | mesh-abi-base |
| @ajax/mesh-abi-input-queue | `mesh/abi/input_queue.h` | `IQ_HEAD`, `IQ_TAIL`, `IQ_DATA` | — |
| @ajax/mesh-abi-snapshot | `mesh/abi/snapshot.h` | `SNAP_VERSION`, `SN_TOTAL`, `SN_COUNT`, `SN_VERSION`, `SN_HOST_AT`, `SN_HEAD_BYTES` … (17) | mesh-abi-program-table, mesh-abi-node, mesh-abi-ext |
| @ajax/mesh-abi-host-status | `mesh/abi/host_status.h` | `S_UPTIME_MS`, `S_EPOCH`, `S_SYSTEM`, `S_PEER_STATE`, `S_PROGRAMS`, `S_SMS_TOTAL` … (32) | — |
| @ajax/mesh-abi-host-members | `mesh/abi/host_members.h` | `S_MEMBERS`, `MEMBER_BYTES`, `M_STATE`, `M_CC`, `M_SMS`, `M_SMS_TOTAL` … (21) | mesh-abi-fields |
| @ajax/mesh-abi-host-net | `mesh/abi/host_net.h` | `S_NET_AT_MS`, `S_NET_NICS`, `S_NET_SAMPLES`, `S_CTL_RX`, `S_CTL_TX`, `S_CTL_REQS` … (10) | — |
| @ajax/mesh-abi-host-links | `mesh/abi/host_links.h` | `S_LINKS`, `LINK_BYTES`, `L_RX_BYTES`, `L_TX_BYTES`, `L_RX_FRAMES`, `L_TX_FRAMES` … (15) | mesh-abi-peer-frames, mesh-abi-fields |
| @ajax/mesh-abi-host-nics | `mesh/abi/host_nics.h` | `S_NICS`, `NIC_BYTES`, `MAX_NICS`, `NI_NAME`, `NI_RX_BYTES`, `NI_TX_BYTES` … (16) | mesh-abi-fields |
| @ajax/mesh-abi-host-load | `mesh/abi/host_load.h` | `S_LOAD`, `S_LOAD_END`, `LOAD_WINDOW`, `S_LOAD_AT_MS`, `S_LOAD_SAMPLES`, `S_LOAD_FLAGS` … (69) | — |
| @ajax/mesh-abi-host-mload | `mesh/abi/host_mload.h` | `S_MLOAD`, `MLOAD_BYTES`, `ML_AT_MS`, `ML_SHARE`, `ML_SHARE10`, `ML_MAX_GAP_US` … (19) | mesh-abi-fields |
| @ajax/mesh-abi-host-fleet | `mesh/abi/host_fleet.h` | `S_FLEET`, `S_LEADER`, `S_ROLE`, `S_TERM`, `S_STATE_VER`, `S_APPLIED_VER` … (41) | mesh-abi-fields |
| @ajax/mesh-abi-host-sched | `mesh/abi/host_sched.h` | `S_SCHED`, `S_SCHED_FLAGS`, `S_SCHED_MOVES`, `S_SCHED_GAP_US`, `S_CORDON`, `S_BUSY_MASK` … (18) | — |
| @ajax/mesh-abi-host-jobs | `mesh/abi/host_jobs.h` | `S_JOBS`, `S_JOBS_ACTIVE`, `S_JOBS_TOTAL`, `S_JOB_DONE`, `S_JOB_FAILED`, `S_JOB_LEASED` … (16) | — |
| @ajax/mesh-abi-host-pload | `mesh/abi/host_pload.h` | `S_PLOAD`, `PLOAD_BYTES`, `PL_FLAGS`, `PL_LAT_N`, `PL_SHARE`, `PL_SHARE10` … (31) | mesh-abi-fields |
| @ajax/mesh-abi-host-fields | `mesh/abi/host_fields.h` | `HOST_FIELDS` | mesh-abi-base, mesh-abi-host-status, mesh-abi-host-members, mesh-abi-host-net, mesh-abi-host-links, mesh-abi-host-nics, mesh-abi-host-load, mesh-abi-host-mload, mesh-abi-host-fleet, mesh-abi-host-sched, mesh-abi-host-jobs, mesh-abi-host-pload, mesh-abi-fields |
| @ajax/mesh-abi-native | `mesh/abi/native.h` | `NATIVE_ABI`, `NATIVE_ENV_ABI`, `NATIVE_ENV_NAME`, `NATIVE_STATUS_FD`, `NATIVE_MAILBOX_FD` | — |
| @ajax/mesh-abi-placement | `mesh/abi/placement.h` | `PLACE_CAP_NONE`, `PLACE_CAP_PIN`, `PLACE_CAP_SPLIT`, `PLACE_CAPS`, `PLACE_LOCAL`, `PLACE_PIN` … (19) | — |
| @ajax/mesh-abi-peer-frames | `mesh/abi/peer_frames.h` | `PEER_PORT`, `PEER_VERSION`, `F_HELLO`, `F_ROUND`, `F_BEAT`, `F_VOTE_REQ` … (37) | — |
| @ajax/mesh-abi-peer-sizes | `mesh/abi/peer_sizes.h` | `F_PERF_MAX`, `VIEW_REQ_BYTES`, `VIEW_HEAD`, `F_VIEW_CHUNK`, `F_BLOB_MAX`, `JOB_PAYLOAD_MAX` … (31) | mesh-abi-base |
| @ajax/mesh-abi-reconcile | `mesh/abi/reconcile.h` | `RS_NONE`, `RS_SYNC`, `RS_PENDING`, `RS_FAILED`, `RS_NAMES` | — |
| @ajax/mesh-abi-group-state | `mesh/abi/group_state.h` | `GL_NONE`, `GL_PAUSED`, `GL_HELLO`, `GL_RUNNING`, `GL_NAMES` | — |
| @ajax/mesh-abi-place-ops | `mesh/abi/place_ops.h` | `PL_REQ`, `PL_RESULT`, `PL_PAUSE`, `PL_PAUSED`, `PL_SET`, `PL_DONE` … (7) | — |
| @ajax/mesh-abi-pipe-ring | `mesh/abi/pipe_ring.h` | `pipe::PI_HEAD`, `pipe::PI_FULL`, `pipe::PI_TAIL`, `pipe::PI_EMPTY`, `pipe::PI_LAPS`, `pipe::REC_MAX` … (11) | mesh-abi-words, mesh-abi-pipe-hash |
| @ajax/mesh-abi-pipe | `mesh/abi/pipe.h` | `pipe::PIPE_OK`, `pipe::PIPE_NONE`, `pipe::PIPE_NOT_CONNECTED`, `pipe::PIPE_SIZE`, `pipe::PIPE_GONE` | mesh-abi-base, mesh-abi-header, mesh-abi-program-table, mesh-abi-pipe-table, mesh-abi-pipe-ring, mesh-abi-words, mesh-abi-pipe-hash, mesh-abi-view-heap |
| @ajax/mesh-abi-view-train | `mesh/abi/view_train.h` | `train::T_PARAMS`, `train::T_LR`, `train::T_STEPS_PER_TASK`, `train::T_INV_BATCH`, `train::T_NEXT_STEP`, `train::T_ERR` … (20) | — |
| @ajax/mesh-abi-view-infer | `mesh/abi/view_infer.h` | `infer::I_PARAMS`, `infer::I_RUNS`, `infer::I_TRAIN_STEP`, `infer::I_X`, `infer::I_Y`, `infer::POINTS` … (10) | — |
| @ajax/mesh-abi-view-doom | `mesh/abi/view_doom.h` | `doom::WIDTH`, `doom::D_POS_X`, `doom::D_POS_Y`, `doom::D_DIR_X`, `doom::D_DIR_Y`, `doom::D_PLANE_X` … (43) | mesh-abi-placement |
| @ajax/mesh-abi-view-http | `mesh/abi/view_http.h` | `http::HV_SERVED`, `http::HV_LAST_CODE`, `http::HV_LOAD_SEQ`, `http::HV_LOAD_ACK`, `http::HV_LOAD_RESULT`, `http::HV_LOAD_NAME` … (15) | — |
| @ajax/mesh-abi-view-pipedemo | `mesh/abi/view_pipedemo.h` | `pipedemo::MSG_TYPE`, `pipedemo::MSG_BYTES`, `pipedemo::DIGEST_TYPE`, `pipedemo::DIGEST_BYTES`, `pipedemo::R_SEQ`, `pipedemo::R_STAMP_LO` … (53) | sha256-compress, hist-quantile |
| @ajax/mesh-abi-view-gpt2x | `mesh/abi/view_gpt2x.h` | `gpt2x::X_MAGIC`, `gpt2x::MAGIC`, `gpt2x::X_PARAMS`, `gpt2x::PARAM_WORDS`, `gpt2x::X_TOKENS`, `gpt2x::X_WINDOW` … (44) | — |
| @ajax/mesh-abi-wasm | `mesh/abi/wasm.h` | `wasm::MODULE`, `wasm::EXPORT_MEMORY`, `wasm::EXPORT_INIT`, `wasm::EXPORT_TASK`, `wasm::EXPORT_RT_ABI`, `wasm::FTZ_PREFIX` … (27) | — |

## Dependency graph

Arrows point at dependencies (the test-only `@ajax/mesh-check` is left out).

```
hist-quantile
mesh-abi -> every mesh-abi-* package
mesh-abi-backend
mesh-abi-base
mesh-abi-control-block -> mesh-abi-fields
mesh-abi-control-protocol
mesh-abi-exchange
mesh-abi-ext -> mesh-abi-base, mesh-abi-control-block, mesh-abi-placement, mesh-abi-fields
mesh-abi-fault
mesh-abi-fields
mesh-abi-fnv
mesh-abi-group-state
mesh-abi-header -> mesh-abi-fields
mesh-abi-host-fields -> mesh-abi-base, mesh-abi-host-status, mesh-abi-host-members, mesh-abi-host-net, mesh-abi-host-links, mesh-abi-host-nics, mesh-abi-host-load, mesh-abi-host-mload, mesh-abi-host-fleet, mesh-abi-host-sched, mesh-abi-host-jobs, mesh-abi-host-pload, mesh-abi-fields
mesh-abi-host-fleet -> mesh-abi-fields
mesh-abi-host-jobs
mesh-abi-host-links -> mesh-abi-peer-frames, mesh-abi-fields
mesh-abi-host-load
mesh-abi-host-members -> mesh-abi-fields
mesh-abi-host-mload -> mesh-abi-fields
mesh-abi-host-net
mesh-abi-host-nics -> mesh-abi-fields
mesh-abi-host-pload -> mesh-abi-fields
mesh-abi-host-sched
mesh-abi-host-status
mesh-abi-input-queue
mesh-abi-load-page -> mesh-abi-host-load
mesh-abi-name -> mesh-abi-base
mesh-abi-native
mesh-abi-node -> mesh-abi-fields
mesh-abi-node-load -> mesh-abi-node, mesh-abi-fields
mesh-abi-ops
mesh-abi-peer-frames
mesh-abi-peer-sizes -> mesh-abi-base
mesh-abi-pipe -> mesh-abi-base, mesh-abi-header, mesh-abi-program-table, mesh-abi-pipe-table, mesh-abi-pipe-ring, mesh-abi-words, mesh-abi-pipe-hash, mesh-abi-view-heap
mesh-abi-pipe-hash
mesh-abi-pipe-ring -> mesh-abi-words, mesh-abi-pipe-hash
mesh-abi-pipe-table -> mesh-abi-base, mesh-abi-fields
mesh-abi-place-ops
mesh-abi-placement
mesh-abi-program-table -> mesh-abi-base
mesh-abi-ptx-check -> mesh-abi
mesh-abi-reconcile
mesh-abi-ring
mesh-abi-snapshot -> mesh-abi-program-table, mesh-abi-node, mesh-abi-ext
mesh-abi-trace
mesh-abi-view-doom -> mesh-abi-placement
mesh-abi-view-gpt2x
mesh-abi-view-heap -> mesh-abi-base
mesh-abi-view-http
mesh-abi-view-infer
mesh-abi-view-pipedemo -> sha256-compress, hist-quantile
mesh-abi-view-train
mesh-abi-wasm
mesh-abi-wasm-check -> mesh-abi
mesh-abi-words
mesh-check
mesh-words-host -> mesh-abi-words
sha256-compress
```

## Not ported

Nothing in the crate. Rust-only mechanics with no C counterpart: `core::hint::spin_loop()` in the bounded
ring loops (dropped; the loops are bounded by `TRIES` either way), and `#![no_std]` (the C headers are freestanding).
