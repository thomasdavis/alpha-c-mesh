/* @ajax/mesh-manifest-needs: what a program needs and prefers (its manifest's
 * scheduling keys), ported from mesh-host sched.rs's Replicas, Migrate, Term
 * and Needs.  The manifest parser fills it; the fleet scheduler reads it.
 * Term::matches (which needs the scheduler's Member) stays with the scheduler. */
#ifndef MESH_NEEDS_H
#define MESH_NEEDS_H

#include "mesh/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Replicas: All, or N(n). */
typedef struct {
    bool all;
    uint32_t n;
} mesh_replicas;

/* How the scheduler may move a program: with its view, by a fresh start elsewhere, or not at all. */
typedef enum { MESH_MIGRATE_STATE, MESH_MIGRATE_RESTART, MESH_MIGRATE_NEVER } mesh_migrate;
const char *mesh_migrate_word(mesh_migrate m); /* "state", "restart", "never" */

/* A placement term (`prefer`, `avoid`, `group_members`). */
typedef enum { MESH_TERM_BACKEND, MESH_TERM_HOST, MESH_TERM_SYSTEM, MESH_TERM_ARCH } mesh_term_kind;
typedef struct {
    mesh_term_kind kind;
    char *s;         /* BACKEND, HOST, ARCH */
    uint32_t system; /* SYSTEM */
} mesh_term;

/* "cuda", "host:NAME", "system:K" (K < MAX_SYSTEMS), "arch:A". False with *err:
 * "\"x\": a backend (cuda, metal, wasm, native), \"host:NAME\", \"system:K\" or \"arch:A\"". */
bool mesh_term_parse(const char *s, mesh_term *out, char **err);
char *mesh_term_word(const mesh_term *t); /* "cuda", "host:dgx-00", "system:3", "arch:x86_64" */
bool mesh_term_eq(const mesh_term *a, const mesh_term *b);
void mesh_term_copy(mesh_term *dst, const mesh_term *src);
void mesh_term_free(mesh_term *t);

typedef struct mesh_needs {
    char **backends;
    size_t nbackends;
    char *arch; /* NULL: any */
    uint32_t min_mem_mb;
    mesh_term *prefer;
    size_t nprefer;
    mesh_term *avoid;
    size_t navoid;
    mesh_replicas replicas;
    bool pack;
    char *colocate_with; /* NULL: none */
    /* The programs it feeds (pipes): a migration stops a producer before its consumer. */
    char **feeds;
    size_t nfeeds;
    char *group; /* NULL: none */
    uint32_t group_size; /* 0: every eligible member */
    mesh_term *group_members;
    size_t ngroup_members;
    int32_t priority;
    /* Reservations: CPU cores and MiB. */
    float cpu;
    uint32_t mem_mb;
    mesh_migrate migrate;
    bool lockstep;
    uint32_t view_bytes;
    /* A GPU node's shape (manifest warps, shared_bytes). */
    uint32_t warps;
    uint32_t shared;
} mesh_needs;

/* Needs::default(): backends ["cuda"], replicas All, migrate State, warps 1. */
void mesh_needs_default(mesh_needs *n);
void mesh_needs_copy(mesh_needs *dst, const mesh_needs *src);
bool mesh_needs_eq(const mesh_needs *a, const mesh_needs *b);
void mesh_needs_free(mesh_needs *n);
/* Rust's derived `{:?}`: Needs { backends: ["cuda"], arch: None, ... }. */
void mesh_needs_debug(mesh_text *t, const mesh_needs *n);
void mesh_term_debug(mesh_text *t, const mesh_term *x);

/* Helpers for string lists ({:?} of Vec<String>, copies). */
void mesh_strs_debug(mesh_text *t, char *const *v, size_t n);
char **mesh_strs_copy(char *const *v, size_t n);
void mesh_strs_free(char **v, size_t n);
bool mesh_strs_eq(char *const *a, size_t an, char *const *b, size_t bn);

#endif
