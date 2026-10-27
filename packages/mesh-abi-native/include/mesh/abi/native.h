/* @ajax/mesh-abi-native: The native worker contract: its ABI, environment and file descriptors.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_NATIVE_H
#define MESH_ABI_NATIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- native workers */
/* A program whose manifest says `backends = ["native"]` is an executable,
 * built per target into the catalog at `<dir>/<target triple>/<name>`, that
 * links the `mesh-worker` SDK.  meshd runs it as a *worker process* beside
 * its GPU backend (CUDA or Metal): the process maps meshd's mailbox (a
 * shared-memory file: a memfd on Linux, shm_open on macOS, also the GPU's
 * mailbox) and consumes its slot's ring exactly as mesh-rt's nodes do --
 * tickets, E_SEQ, P_RESULTS/P_MARKS (release), P_DONE, node records, trace,
 * P_ONLINE/P_HALTED -- with one thread per node.  So the publisher, control,
 * snapshots, replication and placement treat it like any program.
 *
 * The process gets (environment and inherited descriptors):
 * NATIVE_ENV_ABI = NATIVE_ABI, NATIVE_ENV_NAME = the program's name,
 * fd NATIVE_STATUS_FD: a pipe to meshd, fd NATIVE_MAILBOX_FD: the mailbox
 * (MAILBOX_BYTES, map it MAP_SHARED), stdin: meshd's commands,
 * stdout/stderr: the program's log (meshd's `log` and `<logs>/<name>.log`).
 * Lines, worker -> meshd (status fd): `ready <NATIVE_ABI>` once it is set up
 * (the program's own setup before `mesh_worker::run` included), then
 * `halted <gen>` after each launch's nodes all halted.
 * Lines, meshd -> worker (stdin): `run <gen> <slot>`: start the slot's
 * PT_NODES nodes (each P_ONLINE += 1; the entry is read at that moment);
 * `exit`: leave.  EOF on stdin (meshd gone, however it died) stops the nodes
 * and exits.  P_SHUTDOWN = SHUTDOWN_HALT: the nodes halt and the process
 * waits for the next `run` (a blink, an upgrade's retained image: rollback
 * and relaunch need no new process); SHUTDOWN_EXIT: they halt and it exits.
 * Native nodes ignore H_BLINK: a CUDA whole-context blink never includes a
 * native slot (it changes no module of theirs). */
#define MESH_NATIVE_ABI 1u
#define MESH_NATIVE_ENV_ABI "MESH_NATIVE_ABI"
#define MESH_NATIVE_ENV_NAME "MESH_PROGRAM"
#define MESH_NATIVE_STATUS_FD 3
#define MESH_NATIVE_MAILBOX_FD 4

#endif
