/* @ajax/mesh-abi-control-protocol: meshd's control port and HTTP port, and the reasons a load can fail.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_CONTROL_PROTOCOL_H
#define MESH_ABI_CONTROL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* meshd's control port (127.0.0.1), HTTP port (8088: 8080 is held by other
 * services on both DGX machines) and peer link port. */
#define MESH_CONTROL_PORT 7700
#define MESH_HTTP_PORT 8088
/* ---------------------------------------------------------------- control protocol v2 */
/* meshd's control port (CONTROL_PORT): one text line per request; replies are
 * one line (`ok ...` / `err <reason>`) unless marked *framed* (u32 LE length,
 * then that many bytes).  `<name>` is a loaded program's name.
 * snap                       -> framed: snapshot v2 (below)
 * programs                   -> framed text, one line per non-empty slot:
 * `slot name state nodes sms policy tasks/s ptx_hash regs view_off+view_bytes`
 * then `fields <name> <field>...` and `counters <name> <c0>..<c3>` lines
 * catalog                    -> framed text: the program dirs under --programs DIR, one per line
 * load <dir|program.toml>    -> ok slot <s> blink <us>   | err <reason>
 * reasons: manifest: .. | abi | name exists | no slot | view heap |
 * occupancy | regs | jit: <log> | load: <cuda err>; paths are
 * confined to --programs DIR
 * upgrade <name> [dir]       -> ok blink <us> view kept|reset
 * rollback <name>            -> ok blink <us>
 * unload <name> [--now]      -> ok                       (module and view freed at the next blink)
 * start <name> / stop <name> -> ok                       (zero gap: no module change)
 * task <name> <op> <arg>     -> ok <result>
 * req <name> <len>\n<bytes>  -> framed bytes             (the request window; OP_REQ)
 * input <name> <bytes>       -> ok                       (appends to the input queue)
 * reset <name>               -> ok                       (stop, zero the view, OP_INIT, restore run state)
 * peek <name> <off> <len>    -> framed bytes             (read-only view access)
 * log [n]                    -> framed text              (the last n meshd log lines)
 * trace                      -> framed: per non-empty slot, u32 slot, u32 P_TRACE_N, then its
 * TRACE_PER_PROG x TRACE_BYTES records verbatim
 * place <name>               -> ok <mode> role <role> system <k> paused <b> shown <n> lat_n <n>
 * lat_sum_us <n> lat_max_us <us> ignored <n> uptime_ms <ms>
 * place <name> local|pin <k>|split [spec] -> ok version <v> ...   (a fleet change; "placement" above)
 * fleet                      -> framed text: the leader, term, version committed / accepted /
 * applied, the up and suspect members, each member's reconcile
 * status, the desired state's programs, groups, places
 * groups                     -> framed text: each lockstep group, its epoch, members, state and
 * every member's data-plane state
 * group <name> reform | group <name> on_loss pause|reform [<ms>] -> ok version <v> ...
 * leader                     -> ok system <k> term <t> | err peer: no leader ...
 * cluster                    -> framed text: `cluster <N> system <k> state <up|no-leader> leader <k>
 * up <mask> linked <mask>`, then per member `member <k> <absent|linked|up|suspect> rtt_us <f>
 * wait_us <f> rounds <n> round <n> cc <c> sms <n>/<total>
 * blocks_per_sm <n> arch <a> load_age_ms <ms|-> gpu_share <f|-> cpu <f>
 * psi_cpu <f> psi_mem <f> meshd_cpu <f> mem_avail_mb <n> gpu <name...>`
 * load                       -> framed text: this machine's load (S_LOAD..), `key value` lines:
 * gpu, cpu, cores (per core), memory, meshd, disk, then one
 * `program <name> ...` line per loaded program (PL_*)
 * Sugar: `http <path>` = a GET through `req http`, `key <k>` = input doom <k>,
 * `train-reset` = reset train.  In a fleet, load/upgrade/rollback/unload/start/
 * stop/reset/place, `group ...` and the lockstep program's task change the
 * desired state: forwarded to the leader (F_CTL), committed on a majority,
 * reconciled by every member, answered `ok version <v> peer ok: <what>
 * (system 0 ok: ..; system 3 absent: reconcile on join)` / `err ... peer err (..)`;
 * without a leader `err peer: no leader ...`.  The lockstep program's
 * upgrade/rollback/task are cycles of its group's epoch, its reset a new epoch.
 * `unsupported`: the manifest's `backends` does not list this meshd's
 * backend (a member reports it and is not a failure; a lockstep group is
 * formed of the members that can run it).
 * `float`: a wasm image whose manifest declares the cluster float mode
 * (`float = "ftz"`, the default; required by `peer = "lockstep"`) has f32
 * arithmetic outside the runtime's FTZ helpers, SIMD, threads or relaxed-SIMD
 * opcodes (mesh-host wasm.rs `float_check`).  `exec`: an upgrade or rollback
 * would move the slot to another executor (cuda/metal <-> wasm). */
#define MESH_LOAD_REASONS_LEN 13
extern const char *const MESH_LOAD_REASONS[13];

#endif
