/* Rust's Debug and Display formats, by Rust's own rules (core::fmt, core::time). */
#include "mesh/rsfmt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mesh_rs_debug_str(mesh_buf *b, const char *s, size_t n) {
    mesh_buf_putc(b, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': mesh_buf_puts(b, "\\\""); break;
        case '\\': mesh_buf_puts(b, "\\\\"); break;
        case '\n': mesh_buf_puts(b, "\\n"); break;
        case '\r': mesh_buf_puts(b, "\\r"); break;
        case '\t': mesh_buf_puts(b, "\\t"); break;
        case 0: mesh_buf_puts(b, "\\0"); break;
        default:
            if (c < 0x20 || c == 0x7f) mesh_buf_printf(b, "\\u{%x}", c);
            else mesh_buf_putc(b, (char)c);
        }
    }
    mesh_buf_putc(b, '"');
}

void mesh_rs_debug_cstr(mesh_buf *b, const char *s) { mesh_rs_debug_str(b, s, strlen(s)); }

/* core::time's fmt_decimal without a precision: the integer part, then the
 * fraction's significant digits (trailing zeros never printed). */
static void decimal(mesh_buf *b, uint64_t integer, uint32_t frac, uint32_t divisor, const char *unit) {
    mesh_buf_printf(b, "%llu", (unsigned long long)integer);
    if (frac > 0) {
        mesh_buf_putc(b, '.');
        while (frac > 0 && divisor > 0) {
            mesh_buf_putc(b, (char)('0' + frac / divisor));
            frac %= divisor;
            divisor /= 10;
        }
    }
    mesh_buf_puts(b, unit);
}

void mesh_rs_duration(mesh_buf *b, uint64_t secs, uint32_t nanos) {
    if (secs > 0) decimal(b, secs, nanos, 100000000u, "s");
    else if (nanos >= 1000000u) decimal(b, nanos / 1000000u, nanos % 1000000u, 100000u, "ms");
    else if (nanos >= 1000u) decimal(b, nanos / 1000u, nanos % 1000u, 100u, "\xc2\xb5s");
    else decimal(b, nanos, 0, 1, "ns");
}

/* digits (no point) and the decimal exponent of the first digit: value = 0.d1d2.. x 10^(exp+1). */
static void plain(mesh_buf *b, int neg, const char *digits, int exp) {
    size_t n = strlen(digits);
    if (neg) mesh_buf_putc(b, '-');
    if (exp < 0) {
        mesh_buf_puts(b, "0.");
        for (int i = 0; i < -exp - 1; i++) mesh_buf_putc(b, '0');
        mesh_buf_puts(b, digits);
    } else if ((size_t)exp + 1 >= n) {
        mesh_buf_puts(b, digits);
        for (size_t i = n; i < (size_t)exp + 1; i++) mesh_buf_putc(b, '0');
    } else {
        mesh_buf_put(b, digits, (size_t)exp + 1);
        mesh_buf_putc(b, '.');
        mesh_buf_puts(b, digits + exp + 1);
    }
}

/* The shortest %.{p}e that reads back to v, as digits and exponent. */
static void shortest(mesh_buf *b, double v, int is_f32) {
    if (isnan(v)) { mesh_buf_puts(b, "NaN"); return; }
    if (isinf(v)) { mesh_buf_puts(b, v < 0 ? "-inf" : "inf"); return; }
    int neg = signbit(v) != 0;
    if (v == 0) { mesh_buf_puts(b, neg ? "-0" : "0"); return; }
    double a = fabs(v);
    char s[64];
    for (int p = 0; p < 17; p++) {
        snprintf(s, sizeof s, "%.*e", p, a);
        int back = is_f32 ? (strtof(s, NULL) == (float)a) : (strtod(s, NULL) == a);
        if (back || p == 16) break;
    }
    /* s is d[.ddd]e[+-]xx */
    char digits[40];
    size_t k = 0;
    const char *e = strchr(s, 'e');
    for (const char *c = s; c < e; c++) if (*c != '.') digits[k++] = *c;
    digits[k] = 0;
    while (k > 1 && digits[k - 1] == '0') digits[--k] = 0;
    plain(b, neg, digits, atoi(e + 1));
}

void mesh_rs_f32(mesh_buf *b, float v) { shortest(b, v, 1); }
void mesh_rs_f64(mesh_buf *b, double v) { shortest(b, v, 0); }

void mesh_rs_fixed(mesh_buf *b, double v, int p) {
    if (isnan(v)) { mesh_buf_puts(b, "NaN"); return; }
    if (isinf(v)) { mesh_buf_puts(b, v < 0 ? "-inf" : "inf"); return; }
    mesh_buf_printf(b, "%.*f", p, v);
}
