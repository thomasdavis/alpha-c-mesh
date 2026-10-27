/* @ajax/mesh-place-mode: a program's placement across a cluster of N machines (mesh-abi
 * "placement"): local, pinned to one machine, or split by an assignment of its parts to systems; the
 * request words that ask for one (`local`, `pin 2`, `split`, `split 0,1`, `split 1:3,2:1`,
 * `split auto`); and each machine's role in a mode. */
#ifndef MESH_PLACE_MODE_H
#define MESH_PLACE_MODE_H

#include "mesh/abi/placement.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A split's assignment: part b (task arg b, 0..fanout) is rendered by system of[b]. The owner
 * (part 0's system) owns the state and applies the input. */
typedef struct {
    uint32_t fanout;
    uint8_t of[MESH_PLACE_PARTS_MAX];
} mpm_assign;

mpm_assign mpm_assign_new(const uint32_t *of, size_t n);
uint32_t mpm_owner(const mpm_assign *a);
uint32_t mpm_mask_of(const mpm_assign *a, uint32_t sys); /* the parts sys renders */
uint32_t mpm_members(const mpm_assign *a);               /* bit k: system k renders a part */
char *mpm_list(const mpm_assign *a);                     /* "0,0,1,2" */
/* count parts each, in the given order of systems (the first owns). */
mpm_assign mpm_counts(const uint32_t *sys, const uint32_t *count, size_t n);
/* The parts dealt over systems in order, contiguously, earlier systems taking the remainder; more
 * systems than parts: the first fanout. */
mpm_assign mpm_even(const uint32_t *systems, size_t n, uint32_t fanout);
/* Weighted by time per part (ns; 0 unknown = the mean of the known): parts go one by one to the
 * member that would finish them soonest; the fastest member owns, the others follow in order. */
mpm_assign mpm_auto(const uint32_t *sys, const uint32_t *ns, size_t n, uint32_t fanout);
bool mpm_assign_eq(const mpm_assign *a, const mpm_assign *b);

uint32_t mpm_bits(uint32_t n); /* bits 0..n */

typedef enum { MPM_LOCAL, MPM_PIN, MPM_SPLIT } mpm_kind;
typedef struct {
    mpm_kind kind;
    uint32_t pin;   /* MPM_PIN */
    mpm_assign a;   /* MPM_SPLIT */
} mpm_mode;

/* A request asks for a mode, or `split auto` over a member mask. */
typedef struct {
    bool is_auto;
    mpm_mode mode;
    uint32_t auto_mask;
} mpm_target;

mpm_mode mpm_local(void);
mpm_mode mpm_pin(uint32_t k);
mpm_mode mpm_split(mpm_assign a);
bool mpm_mode_eq(const mpm_mode *a, const mpm_mode *b);
bool mpm_target_eq(const mpm_target *a, const mpm_target *b);

/* `local` | `pin k` | `split [spec]` for a cluster of n and a program of fanout parts. */
bool mpm_parse(char *const *words, size_t nwords, uint32_t n, uint32_t fanout, mpm_target *out, char **err);
/* (PLACE_*, the system it names: pin's runner, split's owner). */
void mpm_code(const mpm_mode *m, uint32_t *code, uint32_t *sys);
/* PX_PLACE_ASSIGN's bytes. */
void mpm_assign_bytes(const mpm_mode *m, uint8_t out[MESH_PLACE_PARTS_MAX]);
bool mpm_from_code(uint32_t code, uint32_t sys, const uint8_t *assign, size_t n, mpm_mode *out, char **err);
char *mpm_label(const mpm_mode *m);
/* Whether a manifest's `place` capability allows it. */
bool mpm_allowed(const mpm_mode *m, uint32_t cap, char **err);
/* Fits a cluster of n and a program of fanout parts. */
bool mpm_fits(const mpm_mode *m, uint32_t n, uint32_t fanout, char **err);

/* This machine's part in a mode. */
typedef enum {
    MPM_ROLE_LOCAL,    /* its own copy, nothing mirrored */
    MPM_ROLE_RUNNER,   /* pinned here: runs every part, sends each frame to the others */
    MPM_ROLE_MIRROR,   /* pinned elsewhere, or a split without parts here: shows the others' frames */
    MPM_ROLE_OWNER,    /* split, part 0 here: owns the state, runs its parts */
    MPM_ROLE_FOLLOWER, /* split, other parts here: runs them on the owner's state */
} mpm_role;

mpm_role mpm_role_of(const mpm_mode *m, uint32_t me);
bool mpm_runs(mpm_role r); /* the kernel runs here */
uint32_t mpm_input_to(const mpm_mode *m, uint32_t me); /* the machine that applies input */
uint32_t mpm_parts_of(const mpm_mode *m, uint32_t sys, uint32_t fanout);
/* The machine whose state continues across from -> to. */
uint32_t mpm_authority(const mpm_mode *from, const mpm_mode *to);

#endif
