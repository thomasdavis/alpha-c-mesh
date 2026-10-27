/* @ajax/mesh-jobs-log: what a member persists (jobs.rs `Log`, `Entry`, `Replica`, `replay`), in an
 * append-only log of records:
 * - LID: the highest leadership it followed or led (a restarted member never follows an older one
 *   again, and leads above it);
 * - SNAPSHOT: the committed table at commit point (lid, n), which replaces what was before (a
 *   follower's entries of that leadership past n are kept);
 * - ENTRY: an entry (lid, n) received or made, not committed yet: a delta, or a replace (a new
 *   leader's merge, which drops other leaderships' entries);
 * - COMMIT: the entries of lid up to n are committed.
 *
 * Replaying the log rebuilds the member's replica exactly: entries that were not committed stay
 * pending, never in the committed table (review finding 5). A torn tail (a crash mid-append, never
 * acknowledged: review finding 4) ends the replay; a whole record that doesn't decode is corruption. */
#ifndef MESH_JOBS_LOG_H
#define MESH_JOBS_LOG_H

#include "mesh/jobs/op.h"
#include "mesh/jobs/table.h"
#include "mesh/strbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { MJ_LOG_LID = 1, MJ_LOG_SNAPSHOT = 2, MJ_LOG_ENTRY = 3, MJ_LOG_COMMIT = 4 } mj_log_kind;

typedef struct {
    mj_log_kind kind;
    uint64_t lid;
    uint64_t n;   /* SNAPSHOT, ENTRY, COMMIT */
    bool replace; /* ENTRY */
    mj_ops ops;   /* SNAPSHOT, ENTRY */
} mj_log;

void mj_log_free(mj_log *l);
mj_log mj_log_clone(const mj_log *l);
static inline mj_log mj_log_lid(uint64_t lid) { return (mj_log){.kind = MJ_LOG_LID, .lid = lid}; }
static inline mj_log mj_log_commit(uint64_t lid, uint64_t n) { return (mj_log){.kind = MJ_LOG_COMMIT, .lid = lid, .n = n}; }
/* These take the ops. */
static inline mj_log mj_log_snapshot(uint64_t lid, uint64_t n, mj_ops ops) { return (mj_log){.kind = MJ_LOG_SNAPSHOT, .lid = lid, .n = n, .ops = ops}; }
static inline mj_log mj_log_entry(uint64_t lid, uint64_t n, bool replace, mj_ops ops) {
    return (mj_log){.kind = MJ_LOG_ENTRY, .lid = lid, .n = n, .replace = replace, .ops = ops};
}

/* A record: u8 kind, u64 length, the body. Appended to out. */
void mj_log_encode(const mj_log *l, msb *out);
bool mj_log_decode(uint8_t kind, const void *body, size_t n, mj_log *out, char **err);

/* A received or made entry, not committed yet. */
typedef struct {
    uint64_t lid; /* its leadership */
    uint64_t n;
    bool replace;
    mj_ops ops;      /* a delta's */
    mj_table table;  /* a replace's (initialised only when replace) */
} mj_entry;

void mj_entry_free(mj_entry *e);
mj_entry mj_entry_clone(const mj_entry *e);
bool mj_entry_eq(const mj_entry *a, const mj_entry *b);

/* A member's durable job state: what its log replays to. */
typedef struct {
    uint64_t lid;                   /* the highest leadership followed or led */
    uint64_t commit_lid, commit_n;  /* the committed table's commit point */
    mj_table committed;
    mj_entry *pending;              /* entries not committed yet, in order */
    size_t npending, cappending;
} mj_replica;

void mj_replica_init(mj_replica *r);
void mj_replica_free(mj_replica *r);
mj_replica mj_replica_clone(const mj_replica *r);
bool mj_replica_eq(const mj_replica *a, const mj_replica *b);
void mj_replica_apply(mj_replica *r, const mj_log *l);
/* The last entry pending of leadership lid (0: none). */
uint64_t mj_replica_last(const mj_replica *r, uint64_t lid);

typedef struct {
    mj_log *v;
    size_t n, cap;
} mj_logs;
void mj_logs_push(mj_logs *v, mj_log l);
void mj_logs_free(mj_logs *v);
/* The log that replays to r (a compaction). */
mj_logs mj_replica_log(const mj_replica *r);

/* Replays a log file: the replica, the number of records and the bytes of whole records. A torn
 * tail ends it there; a whole record that does not decode is an error (*err: "<why> (record n at
 * byte b)"). */
bool mj_replay(const void *bytes, size_t n, mj_replica *out, size_t *records, size_t *whole, char **err);

#endif
