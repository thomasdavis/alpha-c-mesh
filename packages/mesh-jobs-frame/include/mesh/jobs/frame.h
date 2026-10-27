/* @ajax/mesh-jobs-frame: the job frames on the peer links (jobs.rs `Frame` and what it carries:
 * `Prog`, `Held`, `LeaseMsg`, `ItemRes`, `RS_*`, `ST_*`, `HAVE_*`).
 *
 * On the wire a frame is its type (MESH_F_JOB_AVAIL .. MESH_F_RESULT), a u32 length, then the body,
 * which starts with the leadership id it belongs to. A transfer in parts (ST_PART, F_JOB_HAVE)
 * numbers them (`seq`) and its end says how many there were, so a part dropped on the way is never
 * mistaken for a whole table (review finding 9). */
#ifndef MESH_JOBS_FRAME_H
#define MESH_JOBS_FRAME_H

#include "mesh/jobs/id.h"
#include "mesh/jobs/op.h"
#include "mesh/jobs/ranges.h"
#include "mesh/jobs/spec.h"
#include "mesh/strbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An item's status in F_RESULT. */
#define MESH_JOBS_RS_OK 0u
#define MESH_JOBS_RS_FAIL 1u     /* the task failed (R_JOB_FAIL, or the program faulted): `out` is the reason */
#define MESH_JOBS_RS_RETURNED 2u /* not run (the program stopped, the lease was cut): back to the queue, no try counted */

/* F_JOB_STATE kinds. */
#define MESH_JOBS_ST_DELTA 1u
#define MESH_JOBS_ST_PART 2u
#define MESH_JOBS_ST_SNAP_END 3u
#define MESH_JOBS_ST_REPLACE_END 4u
#define MESH_JOBS_ST_COMMIT 5u

/* F_JOB_HAVE's parts: the committed table (at its commit point), an entry not committed yet (its
 * leadership and number), the end. */
#define MESH_JOBS_HAVE_COMMITTED 1u
#define MESH_JOBS_HAVE_DELTA 2u
#define MESH_JOBS_HAVE_REPLACE 3u
#define MESH_JOBS_HAVE_END 4u

/* A job program running on a member (F_JOB_AVAIL). */
typedef struct {
    char *name;
    uint32_t exec, cap;
} mj_prog;

/* A lease as its member holds it: the items it has not reported yet. */
typedef struct {
    uint64_t lease;
    mj_job_id job;
    char *program;
    mj_ranges items;
} mj_held;

/* A lease as the leader sends it (self-contained: the member may not have the job's spec yet). */
typedef struct {
    uint64_t lease;
    mj_job_id job;
    char *program;
    uint32_t op;
    mj_setup *setup;
    size_t nsetup;
    mj_ranges items;
} mj_lease_msg;

typedef struct {
    uint64_t lease;
    mj_job_id job;
    uint64_t item;
    uint32_t status, result;
    uint8_t *out;
    size_t out_len;
    bool has_blob;
    mj_blob_ref blob;
} mj_item_res;

typedef struct {
    uint8_t ty; /* MESH_F_JOB_AVAIL, _STATE, _ACK, _PULL, _HAVE, MESH_F_LEASE, _CUT, MESH_F_RESULT */
    uint64_t lid;
    mj_prog *progs;      /* AVAIL */
    size_t nprogs;
    mj_held *held;       /* AVAIL; HAVE when kind is HAVE_END */
    size_t nheld;
    uint32_t kind;       /* STATE: ST_*; HAVE: HAVE_* */
    uint64_t n, commit;  /* STATE (n, commit); ACK (n) */
    uint32_t seq;        /* STATE, HAVE */
    mj_ops ops;          /* STATE; HAVE but HAVE_END */
    bool gap;            /* ACK */
    uint64_t src0, src1; /* HAVE: the commit point (committed) or the entry's (leadership, number) */
    mj_lease_msg l;      /* LEASE */
    uint64_t lease, cut; /* CUT */
    mj_item_res *items;  /* RESULT */
    size_t nitems;
} mj_frame;

void mj_prog_free(mj_prog *p);
mj_prog mj_prog_clone(const mj_prog *p);
bool mj_prog_eq(const mj_prog *a, const mj_prog *b);
bool mj_progs_eq(const mj_prog *a, size_t na, const mj_prog *b, size_t nb);
mj_prog *mj_progs_clone(const mj_prog *p, size_t n);
void mj_progs_free(mj_prog *p, size_t n);

void mj_held_free(mj_held *h);
mj_held mj_held_clone(const mj_held *h);
bool mj_held_eq(const mj_held *a, const mj_held *b);

void mj_lease_msg_free(mj_lease_msg *l);
mj_lease_msg mj_lease_msg_clone(const mj_lease_msg *l);
bool mj_lease_msg_eq(const mj_lease_msg *a, const mj_lease_msg *b);

void mj_item_res_free(mj_item_res *r);
mj_item_res mj_item_res_clone(const mj_item_res *r);
bool mj_item_res_eq(const mj_item_res *a, const mj_item_res *b);

void mj_frame_free(mj_frame *f);
mj_frame mj_frame_clone(const mj_frame *f);
bool mj_frame_eq(const mj_frame *a, const mj_frame *b);

/* Constructors: they take ownership of the arrays and ops they're given. */
mj_frame mj_frame_avail(uint64_t lid, mj_prog *progs, size_t nprogs, mj_held *held, size_t nheld);
mj_frame mj_frame_state(uint64_t lid, uint32_t kind, uint64_t n, uint64_t commit, uint32_t seq, mj_ops ops);
mj_frame mj_frame_ack(uint64_t lid, uint64_t n, bool gap);
mj_frame mj_frame_pull(uint64_t lid);
mj_frame mj_frame_have(uint64_t lid, uint32_t kind, uint32_t seq, uint64_t src0, uint64_t src1, mj_ops ops, mj_held *held, size_t nheld);
mj_frame mj_frame_lease(uint64_t lid, mj_lease_msg l);
mj_frame mj_frame_cut(uint64_t lid, uint64_t lease, uint64_t cut);
mj_frame mj_frame_result(uint64_t lid, mj_item_res *items, size_t nitems);

/* The wire bytes: type, u32 length, body. Appended to out. */
void mj_frame_encode(const mj_frame *f, msb *out);
/* A frame of type ty from its body (the bytes after the length); false with *err. */
bool mj_frame_decode(uint8_t ty, const void *body, size_t n, mj_frame *out, char **err);

/* The held leases' encoding (a u32 count, then each), shared with F_JOB_AVAIL and F_JOB_HAVE. */
void mj_w_held(msb *w, const mj_held *h, size_t n);
bool mj_rd_held(mj_rd *r, mj_held **h, size_t *n);

#endif
