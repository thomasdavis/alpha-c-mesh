/* @ajax/rsfmt: text exactly as Rust's format! writes it, for C ports that must print the same bytes.
 *
 * Floats: Rust and glibc both print the exact binary value correctly rounded (ties to even), so
 * printf does the digits; this fixes what differs: NaN is "NaN" (never signed), infinity "inf",
 * the exponent form is "1.5e-3" (no '+', no leading zeros), and widths count characters (code
 * points), not bytes. Every function writes at most cap bytes (NUL included) and returns the
 * length it needed, as snprintf does. */
#ifndef RSFMT_H
#define RSFMT_H

#include <stdbool.h>
#include <stddef.h>

enum {
    RSFMT_PLUS = 1, /* {:+} */
    RSFMT_ZERO = 2, /* {:0w}: sign-aware zero padding (numbers only) */
};

/* {:.prec$} of v (an f32 passed as double prints the same, as the value is exact). */
int rsfmt_fixed(char *out, size_t cap, double v, int prec, int flags);
/* {:.prec$e} */
int rsfmt_exp(char *out, size_t cap, double v, int prec, int flags);
/* {:.prec$} padded to width: align is '<', '>' or '^' ('>' is Rust's default for numbers). */
int rsfmt_fixed_w(char *out, size_t cap, double v, int prec, int width, char align, int flags);
/* {:<w$} {:>w$} {:^w$} of a string: pads with fill to width characters (never truncates). */
int rsfmt_pad(char *out, size_t cap, const char *s, int width, char align, char fill);
/* Unicode scalar values in a UTF-8 string (Rust's s.chars().count()). */
size_t rsfmt_chars(const char *s);
/* The first n characters of s, as a byte length (Rust's s.chars().take(n)). */
size_t rsfmt_prefix(const char *s, size_t n);

#endif
