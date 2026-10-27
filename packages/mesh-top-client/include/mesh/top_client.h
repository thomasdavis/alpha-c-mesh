/* @ajax/mesh-top-client: one background thread per meshd endpoint (crates/mesh-top src/client.rs).
 * A persistent control-port connection polls `snap` and runs the viewer's commands in between;
 * the UI never blocks on the network, it only pops mt_updates from a shared queue. Real time
 * (CLOCK_MONOTONIC), never mesh-top-clock's virtual clock. */
#ifndef MESH_TOP_CLIENT_H
#define MESH_TOP_CLIENT_H

#include "mesh/top_app.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A binary reply larger than this is a protocol error, not data. */
#define MT_MAX_REPLY ((size_t)16 << 20)
#define MT_IO_TIMEOUT_MS 3000u
#define MT_BACKOFF_MIN_MS 100u
#define MT_BACKOFF_MAX_MS 2000u

/* How long a request's reply may take (ms): past meshd's own budget for it. */
uint32_t mt_reply_timeout_ms(const char *line);
/* Whether the request's reply is framed (u32 LE len + bytes) rather than a text line. */
bool mt_binary_reply(const char *line);

/* The updates every endpoint sends to (one receiver). Free the endpoints before the queue. */
typedef struct mt_updates mt_updates;
mt_updates *mt_updates_new(void);
void mt_updates_free(mt_updates *q); /* frees updates never popped */
void mt_updates_push(mt_updates *q, mt_update u);
/* Waits up to timeout_ms (0: don't wait) for an update; false when none came. */
bool mt_updates_pop(mt_updates *q, uint32_t timeout_ms, mt_update *out);
/* Frees an update that was not handed to mt_app_apply. */
void mt_update_free(mt_update *u);

typedef struct mt_endpoint mt_endpoint;
/* Starts the endpoint thread for machine idx. */
mt_endpoint *mt_endpoint_spawn(size_t idx, const char *addr, mt_updates *updates, uint64_t interval_ms);
/* Queues a command line (its reply comes back as an MT_UPDATE_REPLY). */
void mt_endpoint_send(mt_endpoint *e, const char *line);
/* Commands sent and not yet answered (the UI drops repeat keys past a few). */
size_t mt_endpoint_pending(const mt_endpoint *e);
/* The snap poll period; the Doom tab polls faster. */
void mt_endpoint_set_interval(mt_endpoint *e, uint64_t ms);
uint64_t mt_endpoint_interval(const mt_endpoint *e);
const char *mt_endpoint_addr(const mt_endpoint *e);
/* Stops the thread (interrupting any request in flight) and frees the endpoint. */
void mt_endpoint_free(mt_endpoint *e);

#endif
