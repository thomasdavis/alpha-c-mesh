/* @ajax/mesh-sched-needs: the fleet scheduler's side of a program's needs.  The data (Needs, Term,
 * Replicas, Migrate) is @ajax/mesh-manifest-needs's `mesh_needs`; this package adds what needs the
 * scheduler: Term::matches against a member, the one whitespace-free token the fleet's document
 * carries ("b=cuda;ar=-;mm=0;..."), and the one-line summary `sched` prints. */
#ifndef MESH_SCHED_NEEDS_H
#define MESH_SCHED_NEEDS_H

#include "mesh/needs.h"
#include "mesh/mt.h"
#include "mesh/sched_member.h"

#include <stdbool.h>
#include <stdint.h>

/* A backend term matches the member's GPU executor; a host the full or the short hostname;
 * a system the member's id; an arch the member's arch. */
bool msn_term_matches(const mesh_term *t, const msm_member *m);
mesh_term msn_term_new(mesh_term_kind kind, const char *s, uint32_t system);
/* Appends to a term list / a string list (both grown by one; t is taken, s is copied). */
void msn_terms_push(mesh_term **v, size_t *n, mesh_term t);
void msn_strs_push(char ***v, size_t *n, const char *s);
/* A string list joined by sep ("" when empty), and membership. */
char *msn_strs_join(char *const *v, size_t n, const char *sep);
bool msn_strs_has(char *const *v, size_t n, const char *s);
/* "-" or the words joined by ','. */
char *msn_terms_text(const mesh_term *v, size_t n);

/* By-value conveniences over mesh_needs_default / mesh_needs_copy. */
mesh_needs msn_default(void);
mesh_needs msn_clone(const mesh_needs *n);

char *msn_encode(const mesh_needs *n);
/* The document's token back; on error *err is the Rust's message and *out holds nothing. */
bool msn_decode(const char *s, mesh_needs *out, char **err);
/* One line for `sched` (only what differs from the defaults). */
char *msn_summary(const mesh_needs *n);
/* "all" or the count. */
char *msn_replicas_word(mesh_replicas r);

#endif
