/* @ajax/mesh-sched-needs: what a program needs and prefers (its manifest's scheduling keys, carried
 * in the fleet's document): backends, arch, min_mem_mb, prefer / avoid terms (cuda, host:dgx-00,
 * system:2, arch:aarch64), replicas = N | "all", spread | pack, colocate_with, the pipes it feeds, a
 * lockstep group with group_size / group_members, priority, cpu / mem_mb reservations,
 * migrate = state | restart | never, and the node shape (warps, shared bytes).
 *
 * `msn_encode` is one whitespace-free token for the document ("b=cuda;ar=-;mm=0;..."). */
#ifndef MESH_SCHED_NEEDS_H
#define MESH_SCHED_NEEDS_H

#include "mesh/sched_member.h"
#include "mesh/text.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool all;
    uint32_t n; /* when !all */
} msn_replicas;

typedef enum { MSN_STATE, MSN_RESTART, MSN_NEVER } msn_migrate;
const char *msn_migrate_word(msn_migrate m);

/* A placement term (prefer, avoid, group_members). */
typedef enum { MSN_BACKEND, MSN_HOST, MSN_SYSTEM, MSN_ARCH } msn_term_kind;
typedef struct {
    msn_term_kind kind;
    char *s;    /* backend, host or arch */
    uint32_t k; /* system */
} msn_term;

typedef struct {
    msn_term *v;
    size_t n, cap;
} msn_terms;

bool msn_term_parse(const char *s, msn_term *out, char **err);
char *msn_term_word(const msn_term *t);
/* A backend term matches the member's GPU executor; a host the full or the short hostname. */
bool msn_term_matches(const msn_term *t, const msm_member *m);
msn_term msn_term_new(msn_term_kind kind, const char *s, uint32_t k);
void msn_terms_push(msn_terms *l, msn_term t); /* takes t */
void msn_terms_free(msn_terms *l);
msn_terms msn_terms_clone(const msn_terms *l);
bool msn_terms_eq(const msn_terms *a, const msn_terms *b);
/* "-" or the words joined by ','. */
char *msn_terms_text(const msn_terms *l);

typedef struct {
    mt_strs backends;
    char *arch; /* NULL: any */
    uint32_t min_mem_mb;
    msn_terms prefer, avoid;
    msn_replicas replicas;
    bool pack;
    char *colocate_with; /* NULL: none */
    mt_strs feeds;       /* the programs it feeds (pipes): a migration stops a producer first */
    char *group;         /* NULL: none */
    uint32_t group_size; /* 0: every eligible member */
    msn_terms group_members;
    int32_t priority;
    float cpu;           /* reservations: CPU cores and MiB */
    uint32_t mem_mb;
    msn_migrate migrate;
    bool lockstep;
    uint32_t view_bytes; /* the view's size (the member's view heap must hold it) */
    uint32_t warps;      /* a GPU node's shape: 32 x warps threads ... */
    uint32_t shared;     /* ... and RT_SHARED_BYTES + shared bytes of shared memory */
} msn_needs;

/* Needs::default: backends ["cuda"], replicas all, spread, migrate state, warps 1. */
msn_needs msn_default(void);
msn_needs msn_clone(const msn_needs *n);
bool msn_eq(const msn_needs *a, const msn_needs *b);
void msn_free(msn_needs *n);
char *msn_encode(const msn_needs *n);
/* The document's token back; on error *err is the Rust's message and *out holds nothing. */
bool msn_decode(const char *s, msn_needs *out, char **err);
/* One line for `sched` (only what differs from the defaults). */
char *msn_summary(const msn_needs *n);
/* "all" or the count. */
char *msn_replicas_word(msn_replicas r);

#endif
