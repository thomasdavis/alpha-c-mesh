/* @ajax/mesh-control: the control port (control.rs; mesh-abi "control protocol").
 *
 * One text line per request, a text line back (`ok ...` / `err ...`), except
 * the framed replies (u32 LE length + bytes): `snap`, `programs`, `catalog`,
 * `req`, `peek`, `log`, `trace`, `http`, `cluster`, `load`, `fleet`, `groups`,
 * `sched`, `sched explain <name>`, `sched dry-run`, `jobs`, `job status`,
 * `job results`. In a fleet every lifecycle change (load, upgrade, rollback,
 * unload, start, stop, reset, place, the lockstep program's tasks, `group
 * ...`) is a change of the desired state: it goes to the leader, which
 * commits it and answers once the reachable members reconciled it.
 *
 * A request line is at most 4096 bytes, at most 32 clients are served at
 * once, a client idle for 120 s (or not taking a reply within 10 s) is closed. */
#ifndef MESH_CONTROL_H
#define MESH_CONTROL_H

#include "mesh/buf.h"
#include "mesh/shared.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Reads the next n body bytes of the request's connection (`req`); false when short. */
typedef bool (*mesh_control_body)(void *ctx, uint8_t *out, size_t n);

/* One request (its line trimmed) answered into out. */
void mesh_control_handle(mesh_shared *sh, const char *req, mesh_control_body body, void *ctx, mesh_buf *out);
/* Serves clients from a listening socket until the process exits (one thread per client). */
void mesh_control_serve(mesh_shared *sh, int listen_fd);
/* One client on a connected socket (the serve loop's thread body), idle limit in ms. Closes fd. */
void mesh_control_client(mesh_shared *sh, int fd, int64_t idle_ms);

/* The text producers other parts reuse: `programs`, `pipes`, `cluster`, the views. */
void mesh_control_programs(mesh_shared *sh, mesh_buf *out);
void mesh_control_pipes(mesh_shared *sh, mesh_buf *out);
void mesh_control_cluster(mesh_shared *sh, mesh_buf *out);
/* A view (fleet, jobs, pipes) as text or JSON: 0, or -1 and *err ("view \"x\": fleet, jobs or pipes [json]"). */
int mesh_control_view(mesh_shared *sh, const char *which, bool json, mesh_buf *out, char **err);
void mesh_control_pipes_recs(mesh_shared *sh, mesh_recs *out);

/* A fleet-rendered image (`render [<job>|latest] [status|ppm|bmp|raw]`): the jobs area's blob thread
 * answers it when wired; NULL: "the blob thread is gone". */
extern bool (*mesh_control_render)(mesh_shared *sh, char **words, size_t n, uint8_t **out, size_t *len, char **err);

#endif
