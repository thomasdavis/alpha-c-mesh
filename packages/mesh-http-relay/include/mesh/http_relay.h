/* @ajax/mesh-http-relay: the HTTP port (http.rs).
 *
 * The host only moves bytes: routing and responses are the GPU http
 * program's (`req http` through its request window), except the fleet's views
 * `/fleet`, `/jobs` and `/pipes`, GPT-2 inference (`/infer?prompt=..`) and a
 * fleet-rendered image (`/render?job=..`, `/render.bmp?job=..`,
 * `/render.ppm?job=..`), which meshd answers itself -- also where the http
 * program cannot run. A request head must arrive within 2 s; at most 64
 * clients are served at once. */
#ifndef MESH_HTTP_RELAY_H
#define MESH_HTTP_RELAY_H

#include "mesh/shared.h"

#include <stdbool.h>
#include <stddef.h>

/* Serves clients from a listening socket until the process exits. */
void mesh_http_serve(mesh_shared *sh, int listen_fd);
/* One client on a connected socket; closes it. */
void mesh_http_client(mesh_shared *sh, int fd);

/* `GET /fleet|/jobs|/pipes[?query]`: the view and the query (pointing into first). */
bool mesh_http_view_route(const char *first, const char **view, const char **query, size_t *qlen);
/* `GET /render[.bmp|.ppm]?job=<id|latest>`: the format ("status", "bmp", "ppm") and the job (malloc'd). */
bool mesh_http_render_route(const char *first, const char **what, char **job);

#endif
