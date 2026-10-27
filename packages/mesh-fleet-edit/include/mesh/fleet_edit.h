/* @ajax/mesh-fleet-edit: a change to the desired state, as the control port asks for it (any member
 * forwards it to the leader as this text), and the next document it makes (fleet.rs Req, edit,
 * revert). Refusals are the control port's reasons. */
#ifndef MESH_FLEET_EDIT_H
#define MESH_FLEET_EDIT_H

#include "mesh/fleet_doc.h"
#include "mesh/member_list.h"
#include "mesh/sched_needs.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MFE_LOAD,        /* name = the catalog dir */
    MFE_UPGRADE,     /* name, dir (NULL: its own dir) */
    MFE_ROLLBACK,
    MFE_UNLOAD,
    MFE_RUN,         /* name, on */
    MFE_RESET,
    MFE_TASK,        /* name, op, arg: a lockstep program's task, run by every member at the same step */
    MFE_PLACE,       /* name, words */
    MFE_REFORM,      /* group name: a new epoch over every eligible up member */
    MFE_ON_LOSS,     /* group name, on_loss, ms */
    MFE_PIN,         /* name, pin (none: unpin) */
    MFE_SCHED_PAUSE, /* on */
    MFE_AUTOREFORM,  /* on */
    MFE_CORDON,      /* k, on */
} mfe_kind;

typedef struct {
    mfe_kind kind;
    char *name;
    char *dir;
    bool on;
    uint32_t op, arg, k, ms;
    mfd_onloss on_loss;
    mt_strs words;
    ml_opt pin;
} mfe_req;

bool mfe_parse(const char *line, mfe_req *out, char **err);
void mfe_req_free(mfe_req *r);

/* A catalog image as the leader sees it (its own catalog). */
typedef struct {
    char *dir, *name;
    uint64_t hash;
    bool lockstep;
    mt_strs backends;
    bool autostart;
    bool placeable;      /* the manifest's `place` capability and fanout */
    uint32_t place_cap, place_fanout;
    mesh_needs needs;
} mfe_image;

void mfe_image_free(mfe_image *im);

/* What the leader needs to edit the document. */
typedef struct {
    /* Its catalog: the image of dir, or false with *err. */
    bool (*catalog)(void *ctx, const char *dir, mfe_image *out, char **err);
    /* The up members (in system order) whose backend runs a program listing these backends. */
    ml_list (*eligible_up)(void *ctx, const mt_strs *backends);
    void *ctx;
    uint32_t n; /* the fleet's size */
} mfe_ctx;

/* The next document for req (version and term are the proposer's) and a line saying what it does;
 * or false with *err. */
bool mfe_edit(const mfd_doc *doc, const mfe_req *req, const mfe_ctx *cx, mfd_doc *out, char **what, char **err);

/* cur with what the change from before to after did undone (a change that failed on every member):
 * the programs, groups and placement requests it changed get their before value back (or go), its
 * switches theirs; what other changes committed since is kept. */
mfd_doc mfe_revert(const mfd_doc *cur, const mfd_doc *before, const mfd_doc *after);

#endif
