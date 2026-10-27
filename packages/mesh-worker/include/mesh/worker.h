/* @ajax/mesh-worker: the SDK of native worker programs (mesh-worker lib.rs).
 * A native program is an ordinary C program built for the machine it runs
 * on, with full access to its hardware:
 *
 *     static void init(const mesh_worker_ctx *c, void *u) { mesh_ctx_set_u32(c, 0, 0x4f4c4548); }
 *     static uint32_t task(const mesh_worker_ctx *c, uint32_t op, uint32_t arg, void *u) {
 *         if (op != MESH_OP_TASK) return MESH_R_BAD_OP;
 *         mesh_ctx_count(c, 0, 1);
 *         return arg * 3 + 7;
 *     }
 *     int main(void) { mesh_worker_run(&(mesh_worker_program){init, task, NULL}); }
 *
 * meshd starts the process (@ajax/mesh-native), which maps meshd's mailbox
 * and plays mesh-rt's node loop on its slot's ring: one thread per node.
 * init and task run once per ticket on a node's thread, so they must be
 * thread-safe.  A crash (mesh_worker_panic, or a signal) ends the process:
 * meshd marks the program FAILED with the exit status and the output's tail;
 * nothing else stops. */
#ifndef MESH_WORKER_H
#define MESH_WORKER_H

#include "mesh/worker_node.h"

/* The worker's main loop: checks the native ABI, maps meshd's mailbox
 * (NATIVE_MAILBOX_FD), says `ready <abi>` on NATIVE_STATUS_FD, then runs a
 * slot's nodes at each `run <gen> <slot>` until they halt, says `halted
 * <gen>`, and exits when P_SHUTDOWN was SHUTDOWN_EXIT, on `exit`, or when
 * meshd is gone (EOF on stdin).  Call it last in main, after the program's
 * own setup: `ready` tells meshd that setup is done. */
_Noreturn void mesh_worker_run(const mesh_worker_program *p);
/* The program's bug: prints "mesh-worker: panic: <message>" and leaves at
 * once with status 101 (Rust's panic status), no core dump. */
_Noreturn void mesh_worker_panic(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
