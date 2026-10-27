/* @ajax/mesh-jobd-store: the job log (jobd.rs `Store`): an append-only file of job log records
 * (@ajax/mesh-jobs-log), rewritten whole -- the replica's own log -- when it grew past twice its last
 * compact size plus 8 MiB. A write or sync that fails is fatal to the caller (fail-stop); a write that
 * failed half-way is cut back to the last whole record first, so a torn tail is never followed by a
 * record. At start the log is replayed (a torn last record dropped) and compacted. Without a path the
 * table is kept in memory and nothing is written. */
#ifndef MESH_JOBD_STORE_H
#define MESH_JOBD_STORE_H

#include "mesh/jobs/log.h"
#include "mesh/jobs/out.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mjd_store mjd_store;
typedef void (*mjd_log_fn)(void *ctx, const char *line);

/* Opens (creates) the log at path (NULL: in memory), replays it into *rep and rewrites it compacted.
 * Info lines (a torn tail dropped, records replayed) go to log (may be NULL). NULL with *err. */
mjd_store *mjd_store_open(const char *path, mj_replica *rep, mjd_log_fn log, void *ctx, char **err);
void mjd_store_free(mjd_store *s);
bool mjd_store_persisted(const mjd_store *s);
/* Rewrites the log as r's own (a compaction): through a renamed file, so a failure leaves it as it was. */
bool mjd_store_rewrite(mjd_store *s, const mj_replica *r, char **err);
/* The log grew past twice its last compact size plus 8 MiB. */
bool mjd_store_wants_compact(const mjd_store *s);
/* The node's disk (mj_durable): appends records, fsyncs on demand. */
mj_disk mjd_store_disk(mjd_store *s);
/* Tests: the next append writes only its first k bytes and fails (a torn write). */
void mjd_store_tear_next(mjd_store *s, size_t k);

#endif
