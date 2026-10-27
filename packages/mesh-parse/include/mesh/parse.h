/* @ajax/mesh-parse: numbers parsed exactly as Rust's `str::parse` accepts them.
 *
 * C's strtoul skips whitespace, accepts "-1" for an unsigned (wrapping it), "0x10" with base 0 and
 * stops at the first bad byte; Rust accepts none of that. The fleet's text formats are parsed by
 * these so a C member accepts and refuses exactly what a Rust member does. All return false on
 * anything Rust's parse would refuse (empty, junk, overflow), leaving *out unchanged. */
#ifndef MESH_PARSE_H
#define MESH_PARSE_H

#include <stdbool.h>
#include <stdint.h>

/* An optional '+', then decimal digits only. */
bool mp_u64(const char *s, uint64_t *out);
bool mp_u32(const char *s, uint32_t *out);
bool mp_u16(const char *s, uint16_t *out);
/* An optional '+' or '-', then decimal digits. */
bool mp_i32(const char *s, int32_t *out);
/* `u64::from_str_radix(s, 16)`: an optional '+', then hex digits of either case. */
bool mp_hex_u64(const char *s, uint64_t *out);
bool mp_hex_u32(const char *s, uint32_t *out);
/* Rust's float grammar: [+-] (digits [. digits] | . digits) [(e|E) [+-] digits], or inf,
 * infinity, nan (any case); no whitespace, no hex floats. */
bool mp_f64(const char *s, double *out);
bool mp_f32(const char *s, float *out);

#endif
