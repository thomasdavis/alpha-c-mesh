/* @ajax/mesh-abi: the whole mesh contract in one include, and the Rust const asserts that span
 * its packages. Prefer the atomic packages (@ajax/mesh-abi-*) in new code: depend on what you use.
 * Ported from alpha-rust-mesh crates/mesh-abi (`use mesh_abi::v2::*`). */
#ifndef MESH_ABI_H
#define MESH_ABI_H

#include "mesh/abi/base.h"
#include "mesh/abi/control_protocol.h"
#include "mesh/abi/backend.h"
#include "mesh/abi/header.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/control_block.h"
#include "mesh/abi/ring.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/node.h"
#include "mesh/abi/node_load.h"
#include "mesh/abi/trace.h"
#include "mesh/abi/exchange.h"
#include "mesh/abi/ext.h"
#include "mesh/abi/load_page.h"
#include "mesh/abi/pipe_table.h"
#include "mesh/abi/view_heap.h"
#include "mesh/abi/input_queue.h"
#include "mesh/abi/snapshot.h"
#include "mesh/abi/host_status.h"
#include "mesh/abi/host_members.h"
#include "mesh/abi/host_net.h"
#include "mesh/abi/host_links.h"
#include "mesh/abi/host_nics.h"
#include "mesh/abi/host_load.h"
#include "mesh/abi/host_mload.h"
#include "mesh/abi/host_fleet.h"
#include "mesh/abi/host_sched.h"
#include "mesh/abi/host_jobs.h"
#include "mesh/abi/host_pload.h"
#include "mesh/abi/host_fields.h"
#include "mesh/abi/native.h"
#include "mesh/abi/placement.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/abi/reconcile.h"
#include "mesh/abi/group_state.h"
#include "mesh/abi/place_ops.h"
#include "mesh/abi/pipe_ring.h"
#include "mesh/abi/pipe.h"
#include "mesh/abi/view_train.h"
#include "mesh/abi/view_infer.h"
#include "mesh/abi/view_doom.h"
#include "mesh/abi/view_http.h"
#include "mesh/abi/view_pipedemo.h"
#include "mesh/abi/view_gpt2x.h"
#include "mesh/abi/wasm.h"
#include "mesh/abi/fnv.h"
#include "mesh/abi/name.h"
#include "mesh/abi/fields.h"
#include "mesh/abi/words.h"
#include "mesh/abi/pipe_hash.h"

/* The Rust const asserts (v2.rs, views.rs) that involve more than one package. */
_Static_assert(MESH_PX_PLACE_ASSIGN + MESH_PLACE_PARTS_MAX <= MESH_PX_PER_SM, "PX_PLACE_ASSIGN + PLACE_PARTS_MAX <= PX_PER_SM");
_Static_assert(MESH_S_CORE_CLASS + MESH_MAX_CPUS <= MESH_S_MLOAD, "S_CORE_CLASS + MAX_CPUS <= S_MLOAD");
_Static_assert(MESH_S_LOAD_END <= MESH_SNAP_HOST, "S_LOAD_END <= SNAP_HOST");
_Static_assert(MESH_S_FLEET_END <= MESH_S_SCHED, "S_FLEET_END <= S_SCHED");
_Static_assert(MESH_S_SCHED_END <= MESH_S_JOBS, "S_SCHED_END <= S_JOBS");
_Static_assert(MESH_S_JOBS_END <= MESH_S_PLOAD, "S_JOBS_END <= S_PLOAD");
_Static_assert(MESH_S_NET_TOTAL + MESH_NIC_BYTES <= MESH_S_LINKS, "S_NET_TOTAL + NIC_BYTES <= S_LINKS");
_Static_assert(MESH_H_BYTES <= MESH_PROG_TABLE, "H_BYTES <= PROG_TABLE");
_Static_assert(MESH_P_COUNTERS + 4 * MESH_COUNTERS <= MESH_P_RING, "P_COUNTERS + 4 * COUNTERS <= P_RING");
_Static_assert(MESH_P_RING == 4 * MESH_LINE, "P_RING == 4 * LINE");
_Static_assert(MESH_P_MARKS + 4 * MESH_PROG_RESULTS  <= MESH_PROG_CTL_BYTES, "P_MARKS + 4 * PROG_RESULTS as usize <= PROG_CTL_BYTES");
_Static_assert(MESH_SC_CTL_BYTES == MESH_P_RING, "SC_CTL_BYTES == P_RING");
_Static_assert(MESH_PROG_NODES + MESH_PROG_NODES_BYTES * MESH_PROG_SLOTS <= MESH_TRACE_BASE, "PROG_NODES + PROG_NODES_BYTES * PROG_SLOTS <= TRACE_BASE");
_Static_assert(MESH_EX_BASE + MESH_EX_BYTES <= MESH_PROG_EXT, "EX_BASE + EX_BYTES <= PROG_EXT");
_Static_assert(MESH_PX_COUNTER_NAMES + MESH_NAME_BYTES * MESH_COUNTERS <= MESH_PX_FIELD_COUNT, "PX_COUNTER_NAMES + NAME_BYTES * COUNTERS <= PX_FIELD_COUNT");
_Static_assert(MESH_PX_PLACE_ASSIGN + MESH_PLACE_PARTS_MAX <= MESH_PX_FIELDS, "PX_PLACE_ASSIGN + PLACE_PARTS_MAX <= PX_FIELDS");
_Static_assert(MESH_PEER_VERSION > MESH_VERSION, "PEER_VERSION > VERSION");
_Static_assert(MESH_VIEW_HEAP_END == MESH_MAILBOX_BYTES, "VIEW_HEAP_END == MAILBOX_BYTES");
_Static_assert(MESH_S_RATES + 4 * MESH_PROG_SLOTS <= MESH_S_LAST_BLINK_MS, "S_RATES + 4 * PROG_SLOTS <= S_LAST_BLINK_MS");
_Static_assert(MESH_S_SESSIONS + 4 <= MESH_S_MEMBERS, "S_SESSIONS + 4 <= S_MEMBERS");
_Static_assert(MESH_S_LOCK_NAME + MESH_NAME_BYTES <= MESH_S_RATES, "S_LOCK_NAME + NAME_BYTES <= S_RATES");
_Static_assert(MESH_SN_HEAD_BYTES + MESH_H_BYTES == 0x110, "SN_HEAD_BYTES + H_BYTES == 0x110");
_Static_assert(MESH_HTTP_HV_SAMPLES + MESH_HTTP_SAMPLE_BYTES * MESH_PROG_SLOTS  <= MESH_HTTP_RESP_OFF, "http::HV_SAMPLES + http::SAMPLE_BYTES * super::PROG_SLOTS as u32 <= http::RESP_OFF");

#endif
