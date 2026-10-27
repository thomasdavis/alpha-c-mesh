/* @ajax/mesh-toml: the value syntax of `program.toml` (mesh-host manifest.rs):
 * integers (decimal or 0x hex, `_` separators), negative integers, floats
 * (with a '.'), "strings" (no escapes), true/false, and one-line lists of
 * strings; `#` comments outside strings. Errors read exactly as the Rust's. */
#ifndef MESH_TOML_H
#define MESH_TOML_H

#include "mesh/text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { MESH_TOML_INT, MESH_TOML_NEG, MESH_TOML_FLOAT, MESH_TOML_STR, MESH_TOML_BOOL, MESH_TOML_LIST } mesh_toml_kind;

typedef struct {
    mesh_toml_kind kind;
    uint32_t n;    /* INT, NEG (the magnitude) */
    double f;      /* FLOAT */
    char *s;       /* STR */
    bool b;        /* BOOL */
    char **items;  /* LIST */
    size_t nitems;
} mesh_toml_val;

/* Rust's `int`: `_` removed, then "0x" hex or decimal u32 (an optional '+'). */
bool mesh_toml_int(const char *s, size_t n, uint32_t *out);
/* "a:b", both `int`s after trimming. */
bool mesh_toml_pair(const char *s, size_t n, uint32_t *a, uint32_t *b);
/* One value (the text after '='). False with *err ("unterminated string",
 * "bad value \"x\"", ...). */
bool mesh_toml_value(const char *v, size_t n, mesh_toml_val *out, char **err);
/* The length of line without a `#` comment that is not inside a string. */
size_t mesh_toml_strip_comment(const char *line, size_t n);
/* Rust's `{:?}` of the value: Int(5), Neg(3), Float(2.0), Str("x"), Bool(true), List(["a", "b"]). */
void mesh_toml_val_debug(mesh_text *t, const mesh_toml_val *v);
char *mesh_toml_val_debug_str(const mesh_toml_val *v);
void mesh_toml_val_free(mesh_toml_val *v);

#endif
