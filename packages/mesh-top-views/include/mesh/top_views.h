/* @ajax/mesh-top-views: the fleet's views as meshd serves them (`view fleet|jobs|pipes` on the
 * control port; the same records as http /fleet, /jobs, /pipes): one record a line,
 * `kind key=value .. [-- free text]`, `-` for none (crates/mesh-top src/views.rs). */
#ifndef MESH_TOP_VIEWS_H
#define MESH_TOP_VIEWS_H

#include "mesh/top_clock.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *key, *value;
} mt_kv;

typedef struct {
    char *kind;
    mt_kv *kv;
    size_t nkv;
    char *text;
} mt_vrec;

/* A field's value; NULL when absent or "-". */
const char *mt_vrec_get(const mt_vrec *r, const char *k);
/* The value, or "-". */
const char *mt_vrec_s(const mt_vrec *r, const char *k);
bool mt_vrec_u(const mt_vrec *r, const char *k, uint64_t *out); /* Rust's u64::from_str */
bool mt_vrec_f(const mt_vrec *r, const char *k, double *out);   /* Rust's f64::from_str */
bool mt_vrec_flag(const mt_vrec *r, const char *k);              /* "1" */
/* A member list ("0,2"); returns the count written (at most cap). */
size_t mt_vrec_list(const mt_vrec *r, const char *k, uint32_t *out, size_t cap);

typedef struct {
    size_t machine;
    mt_instant at;
    mt_vrec *recs;
    size_t n;
    char *err; /* NULL unless the reply was "err ..." */
} mt_view;

/* Parses text (blank lines skipped). Free with mt_vrecs_free. */
size_t mt_views_parse(const char *text, mt_vrec **out);
void mt_vrecs_free(mt_vrec *r, size_t n);
mt_view mt_view_new(size_t machine, mt_instant at, const char *text);
void mt_view_free(mt_view *v);
const mt_vrec *mt_view_first(const mt_view *v, const char *kind);
/* The i-th record of a kind, or NULL. */
const mt_vrec *mt_view_nth(const mt_view *v, const char *kind, size_t i);
size_t mt_view_count(const mt_view *v, const char *kind);
/* Rust's f64::from_str / u64::from_str on a whole string. */
bool mt_parse_f64(const char *s, double *out);
bool mt_parse_u64(const char *s, uint64_t *out);

#endif
