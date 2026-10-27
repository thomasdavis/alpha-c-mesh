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

/* The length of the valid UTF-8 sequence at s (n bytes left), or 0 when it is
 * invalid; *bad then holds the length of the maximal invalid prefix to replace. */
static size_t utf8_seq(const unsigned char *s, size_t n, size_t *bad) {
    unsigned c = s[0];
    size_t need;
    unsigned lo = 0x80, hi = 0xbf;
    if (c < 0x80) return 1;
    if (c >= 0xc2 && c <= 0xdf) need = 1;
    else if (c >= 0xe0 && c <= 0xef) {
        need = 2;
        if (c == 0xe0) lo = 0xa0;
        if (c == 0xed) hi = 0x9f;
    } else if (c >= 0xf0 && c <= 0xf4) {
        need = 3;
        if (c == 0xf0) lo = 0x90;
        if (c == 0xf4) hi = 0x8f;
    } else { *bad = 1; return 0; }
    size_t i = 1;
    for (; i <= need; i++) {
        if (i >= n) { *bad = i; return 0; }
        unsigned x = s[i];
        unsigned l = i == 1 ? lo : 0x80, h = i == 1 ? hi : 0xbf;
        if (x < l || x > h) { *bad = i; return 0; }
    }
    return need + 1;
}

void mesh_rs_lossy(mesh_buf *b, const void *bytes, size_t n) {
    const unsigned char *s = bytes;
    size_t i = 0;
    while (i < n) {
        size_t bad = 0, k = utf8_seq(s + i, n - i, &bad);
        if (k) { mesh_buf_put(b, s + i, k); i += k; }
        else { mesh_buf_puts(b, "\xef\xbf\xbd"); i += bad; }
    }
}

int mesh_rs_utf8_valid(const void *bytes, size_t n) {
    const unsigned char *s = bytes;
    size_t i = 0;
    while (i < n) {
        size_t bad = 0, k = utf8_seq(s + i, n - i, &bad);
        if (!k) return 0;
        i += k;
    }
    return 1;
}

const char *mesh_rs_parse_error(int code) {
    switch (code) {
    case MESH_RS_EMPTY: return "cannot parse integer from empty string";
    case MESH_RS_INVALID: return "invalid digit found in string";
    case MESH_RS_OVERFLOW: return "number too large to fit in target type";
    case MESH_RS_FLOAT_EMPTY: return "cannot parse float from empty string";
    default: return "invalid float literal";
    }
}

int mesh_rs_parse_u64(const char *s, size_t n, uint64_t *out) {
    if (n == 0) return MESH_RS_EMPTY;
    size_t i = 0;
    if (s[0] == '+' && n > 1) i = 1; /* a lone sign is an invalid digit */
    uint64_t x = 0;
    int big = 0;
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return MESH_RS_INVALID;
        unsigned d = (unsigned)(s[i] - '0');
        if (x > (UINT64_MAX - d) / 10) big = 1;
        else x = x * 10 + d;
    }
    if (big) return MESH_RS_OVERFLOW;
    *out = x;
    return 0;
}

int mesh_rs_parse_u32(const char *s, size_t n, uint32_t *out) {
    uint64_t x;
    int r = mesh_rs_parse_u64(s, n, &x);
    if (r == MESH_RS_OVERFLOW || (r == 0 && x > UINT32_MAX)) return MESH_RS_OVERFLOW;
    if (r) return r;
    *out = (uint32_t)x;
    return 0;
}

int mesh_rs_parse_i64(const char *s, size_t n, int64_t *out) {
    if (n == 0) return MESH_RS_EMPTY;
    int neg = s[0] == '-';
    size_t i = (s[0] == '+' || s[0] == '-') && n > 1;
    if (i == 0 && (s[0] == '+' || s[0] == '-')) return MESH_RS_INVALID;
    uint64_t x = 0;
    int big = 0;
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return MESH_RS_INVALID;
        unsigned d = (unsigned)(s[i] - '0');
        if (x > (UINT64_MAX - d) / 10) big = 1;
        else x = x * 10 + d;
    }
    if (big || x > (uint64_t)INT64_MAX + (uint64_t)neg) return MESH_RS_OVERFLOW;
    *out = neg ? (int64_t)(0 - x) : (int64_t)x;
    return 0;
}

static int ieq(const char *s, size_t n, const char *w) {
    if (strlen(w) != n) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != w[i]) return 0;
    }
    return 1;
}

/* core::num::dec2flt's grammar; the value from strtod/strtof (both correctly rounded). */
static int parse_float(const char *s, size_t n, double *d, float *f) {
    if (n == 0) return MESH_RS_FLOAT_EMPTY;
    size_t i = s[0] == '+' || s[0] == '-';
    int neg = s[0] == '-';
    const char *b = s + i;
    size_t m = n - i;
    if (ieq(b, m, "inf") || ieq(b, m, "infinity")) {
        if (d) *d = neg ? -HUGE_VAL : HUGE_VAL;
        if (f) *f = neg ? -HUGE_VALF : HUGE_VALF;
        return 0;
    }
    if (ieq(b, m, "nan")) {
        if (d) *d = NAN;
        if (f) *f = NAN;
        return 0;
    }
    size_t j = 0, intd = 0, frad = 0;
    while (j < m && b[j] >= '0' && b[j] <= '9') { j++; intd++; }
    if (j < m && b[j] == '.') {
        j++;
        while (j < m && b[j] >= '0' && b[j] <= '9') { j++; frad++; }
    }
    if (intd + frad == 0) return MESH_RS_FLOAT_INVALID;
    if (j < m && (b[j] == 'e' || b[j] == 'E')) {
        j++;
        if (j < m && (b[j] == '+' || b[j] == '-')) j++;
        size_t e = 0;
        while (j < m && b[j] >= '0' && b[j] <= '9') { j++; e++; }
        if (!e) return MESH_RS_FLOAT_INVALID;
    }
    if (j != m) return MESH_RS_FLOAT_INVALID;
    char *t = mesh_xstrndup(s, n);
    if (d) *d = strtod(t, NULL);
    if (f) *f = strtof(t, NULL);
    free(t);
    return 0;
}

int mesh_rs_parse_f64(const char *s, size_t n, double *out) { return parse_float(s, n, out, NULL); }
int mesh_rs_parse_f32(const char *s, size_t n, float *out) { return parse_float(s, n, NULL, out); }
