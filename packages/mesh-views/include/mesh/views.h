/* @ajax/mesh-views: the fleet's views (docs/plan-fleet-scheduling.md F6) as records.
 *
 * `/fleet`, `/jobs` and `/pipes` on the http port, `view fleet|jobs|pipes
 * [json]` on the control port, and mesh-top's tabs all read one producer per
 * view as records:
 *
 *     member id=2 state=up leader=0 host=being gpu=cuda share=0.48 -- busy: cpu psi 0.31
 *
 * A record is a kind, `key=value` fields (a value has no spaces or `=`: each
 * becomes `_`; an empty value is `-`, "none") and an optional free text after
 * ` -- `. The text form is one record a line; the JSON form groups records by
 * kind in first-seen order, every record an object of its fields plus "text":
 * `-` is null, a field named in MESH_VIEWS_STRING_KEYS is a string, any other
 * that is a JSON number is a number. */
#ifndef MESH_VIEWS_H
#define MESH_VIEWS_H

#include "mesh/buf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *k, *v;
} mesh_kv;

typedef struct {
    char *kind;
    mesh_kv *kv;
    size_t nkv, cap;
    char *text; /* "" when none */
} mesh_rec;

typedef struct {
    mesh_rec *items;
    size_t len, cap;
} mesh_recs;

#define MESH_RECS_INIT {NULL, 0, 0}

/* A new record at the end of `rs` (the pointer is valid until the next add). */
mesh_rec *mesh_recs_add(mesh_recs *rs, const char *kind);
void mesh_recs_free(mesh_recs *rs);
/* A deep copy appended to dst. */
void mesh_recs_extend(mesh_recs *dst, const mesh_recs *src);

/* A field, its value made one token (whitespace and `=` become `_`, empty becomes `-`). */
void mesh_rec_kv(mesh_rec *r, const char *k, const char *v);
void mesh_rec_kvf(mesh_rec *r, const char *k, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void mesh_rec_kvu(mesh_rec *r, const char *k, uint64_t v);
void mesh_rec_kvi(mesh_rec *r, const char *k, int64_t v);
/* `-` (Rust's `opt` with None). */
void mesh_rec_none(mesh_rec *r, const char *k);
/* A float with p decimals; `-` when !some or not finite. */
void mesh_rec_f(mesh_rec *r, const char *k, bool some, double v, int p);
/* The free text (newlines and carriage returns become spaces). */
void mesh_rec_text(mesh_rec *r, const char *t);
/* A field's value, NULL when absent or `-`. */
const char *mesh_rec_get(const mesh_rec *r, const char *k);

extern const char *const MESH_VIEWS_STRING_KEYS[37];

void mesh_views_text(mesh_buf *out, const mesh_recs *rs);
void mesh_views_json(mesh_buf *out, const mesh_recs *rs);
/* Parses the text form (appends to out). */
void mesh_views_parse(const char *text, mesh_recs *out);
/* `?format=json|text` of a query: 0 and *json set, or -1 with *err (free it). */
int mesh_views_format_of(const char *query, bool *json, char **err);
void mesh_views_render(mesh_buf *out, const mesh_recs *rs, bool json);
/* Whether v is written as a JSON number (exposed for tests). */
bool mesh_views_json_number(const char *v);

#endif
