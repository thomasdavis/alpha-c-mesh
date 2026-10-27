/* @ajax/mesh-rsfmt: the text Rust's formatting produces, for byte-identical replies.
 *
 * meshd's replies and log lines were written with Rust's `{:?}` and `{}`; the C
 * port must say exactly the same. Appends to a mesh_buf. */
#ifndef MESH_RSFMT_H
#define MESH_RSFMT_H

#include "mesh/buf.h"

#include <stddef.h>
#include <stdint.h>

/* `{:?}` of a str: double quotes; \" \\ \n \r \t \0 escaped, other control
 * characters (and DEL) as \u{hex}; everything else as it is. */
void mesh_rs_debug_str(mesh_buf *b, const char *s, size_t n);
void mesh_rs_debug_cstr(mesh_buf *b, const char *s);

/* `{:?}` of a std::time::Duration: 10s, 1.5s, 500ms, 2.5ms, 40µs, 7ns. */
void mesh_rs_duration(mesh_buf *b, uint64_t secs, uint32_t nanos);
static inline void mesh_rs_duration_ms(mesh_buf *b, uint64_t ms) {
    mesh_rs_duration(b, ms / 1000, (uint32_t)(ms % 1000) * 1000000u);
}

/* `{}` of an f32 / f64: the shortest digits that read back to the same value,
 * written without an exponent (1e20 is "100000000000000000000", 1e-7 is
 * "0.0000001"); NaN, inf, -inf; -0 for negative zero. */
void mesh_rs_f32(mesh_buf *b, float v);
void mesh_rs_f64(mesh_buf *b, double v);

/* `{:.p}` of an f64 (as Rust: round half to even on the exact value; NaN, inf). */
void mesh_rs_fixed(mesh_buf *b, double v, int p);

/* String::from_utf8_lossy: valid UTF-8 copied, each maximal invalid
 * subsequence replaced by U+FFFD (as Rust and the Unicode standard do). */
void mesh_rs_lossy(mesh_buf *b, const void *bytes, size_t n);
/* str::from_utf8 succeeds. */
int mesh_rs_utf8_valid(const void *bytes, size_t n);

/* Rust's number parsing (str::parse), for byte-identical acceptance and errors.
 * Return 0, or an error code whose text mesh_rs_parse_error gives:
 * "cannot parse integer from empty string", "invalid digit found in string",
 * "number too large to fit in target type", "invalid float literal",
 * "cannot parse float from empty string". */
enum { MESH_RS_EMPTY = 1, MESH_RS_INVALID, MESH_RS_OVERFLOW, MESH_RS_FLOAT_INVALID, MESH_RS_FLOAT_EMPTY };
int mesh_rs_parse_u64(const char *s, size_t n, uint64_t *out);
int mesh_rs_parse_u32(const char *s, size_t n, uint32_t *out);
int mesh_rs_parse_i64(const char *s, size_t n, int64_t *out);
int mesh_rs_parse_f32(const char *s, size_t n, float *out);
int mesh_rs_parse_f64(const char *s, size_t n, double *out);
const char *mesh_rs_parse_error(int code);

#endif
