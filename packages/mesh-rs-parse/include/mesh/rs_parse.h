/* @ajax/mesh-rs-parse: integers parsed exactly as Rust's `str::parse::<uN/iN>()` and
 * `uN::from_str_radix` do: an optional '+' (or '-' for signed types), then at least one digit of the
 * radix and nothing else (no spaces, no prefix), and a value that fits the type. Strings are
 * (pointer, length), so a word inside a longer line parses without a copy. */
#ifndef MESH_RS_PARSE_H
#define MESH_RS_PARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Unsigned, at most max (UINT64_MAX for u64, UINT32_MAX for u32, ...), radix 2..36. */
bool rs_parse_uint(const char *s, size_t n, unsigned radix, uint64_t max, uint64_t *out);
/* Signed, in [min, max], radix 10. */
bool rs_parse_int(const char *s, size_t n, int64_t min, int64_t max, int64_t *out);

static inline bool rs_u64(const char *s, size_t n, uint64_t *out) { return rs_parse_uint(s, n, 10, UINT64_MAX, out); }
static inline bool rs_u32(const char *s, size_t n, uint32_t *out) {
    uint64_t v;
    if (!rs_parse_uint(s, n, 10, UINT32_MAX, &v)) return false;
    *out = (uint32_t)v;
    return true;
}
static inline bool rs_i32(const char *s, size_t n, int32_t *out) {
    int64_t v;
    if (!rs_parse_int(s, n, INT32_MIN, INT32_MAX, &v)) return false;
    *out = (int32_t)v;
    return true;
}

#endif
