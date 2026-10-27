/* @ajax/rust-fmt: text exactly as Rust's formatting machinery writes it, where C's printf differs.
 *
 * The Rust this repo is ported from builds its wire formats and messages with `format!`. Most of
 * it maps onto printf (`{:.2}` is "%.2f": both round the exact binary value to nearest, ties to
 * even), but three things do not:
 * - `{}` of a float (Display): the shortest digits that read back as the same value, never an
 *   exponent, and no ".0" (1.0 is "1", 1e-7 is "0.0000001", NaN is "NaN");
 * - `{:?}` of a string (Debug): quoted, with Rust's escapes;
 * - `{:#x}`: "0x0" for zero, where C's "%#x" writes "0". */
#ifndef MESH_RUST_FMT_H
#define MESH_RUST_FMT_H

#include <stddef.h>
#include <stdint.h>

/* Room for any f32/f64 Display (f64::MAX is 309 digits, the smallest subnormal ~330 chars). */
#define RF_FLOAT_MAX 400

/* `{}` of an f32 / f64 into out (RF_FLOAT_MAX bytes); returns out. */
char *rf_f32(char out[RF_FLOAT_MAX], float x);
char *rf_f64(char out[RF_FLOAT_MAX], double x);

/* `{:?}` of a string: a new malloc'd string ("\"a\\nb\""). NULL only on out of memory. */
char *rf_debug_str(const char *s);

/* `{:#x}` of a u32 / u64: "0x" then lowercase hex digits, "0x0" for zero; returns out (19+ bytes). */
char *rf_hex_alt(char out[24], uint64_t x);

#endif
