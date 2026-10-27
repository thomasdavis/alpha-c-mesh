/* The job node's private helpers, shared by its source files. */
#ifndef MESH_JOBS_NODE_INTERNAL_H
#define MESH_JOBS_NODE_INTERNAL_H

#include "mesh/jobs/node.h"

/* (job, item, lease) and (job, hash) map keys. */
static inline void mj_key3(uint64_t k[3], mj_job_id job, uint64_t item, uint64_t lease) {
    k[0] = job;
    k[1] = item;
    k[2] = lease;
}
static inline void mj_key5(uint64_t k[5], mj_job_id job, const mj_hash *h) {
    k[0] = job;
    mj_hash_words(h, k + 1);
}

void mj_node_send(mj_node *n, uint32_t k, mj_frame f, mj_outs *out);
void mj_node_log(mj_node *n, mj_log l, mj_outs *out);
void mj_node_pump(mj_node *n, uint64_t now, uint64_t unix_ms, mj_outs *out);
void mj_node_on_frame(mj_node *n, uint32_t from, mj_frame f, uint64_t now, uint64_t unix_ms, mj_outs *out);
mj_outs mj_node_step_down(mj_node *n);
void mj_node_start_lead(mj_node *n, uint64_t term, uint64_t now, uint64_t unix_ms, mj_outs *out);
void mj_node_flush(mj_node *n, mj_outs *out);
void mj_node_snapshots(mj_node *n, mj_outs *out);
void mj_node_commit_check(mj_node *n, mj_outs *out);
void mj_node_dispatch(mj_node *n, uint64_t now, uint64_t unix_ms, mj_outs *out);
void mj_lead_cancel(mj_lead *l, mj_job_id id, const char *why, uint32_t state, uint64_t unix_ms, mj_outs *out);

/* Ops split into the parts they travel in (each part's ops, decoded back as the Rust does). */
typedef struct {
    mj_ops *v;
    size_t n;
} mj_op_parts;
mj_op_parts mj_op_parts_of(const mj_ops *ops);
void mj_op_parts_free(mj_op_parts *p);

char *mj_word(mj_job_id id); /* "t.n", a new string */

#endif
