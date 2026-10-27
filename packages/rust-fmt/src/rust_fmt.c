/* Rust's float Display and string Debug, in C (see the header for why printf is not enough). */
#include "mesh/rust_fmt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The exact decimal expansion of x > 0 (every binary float has a finite one): its digits without
 * the point (no trailing zeros) and the decimal exponent of the first. */
static void exact(double x, char *digits, size_t cap, int *exp10) {
    char buf[1100];
    snprintf(buf, sizeof buf, "%.*e", 1000, x);
    size_t n = 0;
    const char *c = buf;
    for (; *c && *c != 'e' && n + 1 < cap; c++)
        if (*c >= '0' && *c <= '9') digits[n++] = *c;
    while (n > 1 && digits[n - 1] == '0') n--;
    digits[n] = 0;
    while (*c && *c != 'e') c++;
    *exp10 = atoi(c + 1);
}

/* The first p digits of the exact digits, rounded to nearest with ties away from zero (as Rust's
 * shortest Display picks between two equally close candidates): into out, the exponent adjusted on a
 * carry. */
static void round_half_up(const char *ex, int e, int p, char *out, int *exp10) {
    int n = (int)strlen(ex);
    for (int i = 0; i < p; i++) out[i] = i < n ? ex[i] : '0';
    out[p] = 0;
    *exp10 = e;
    if (n <= p || ex[p] < '5') return;
    for (int i = p - 1; i >= 0; i--) {
        if (out[i] < '9') {
            out[i]++;
            return;
        }
        out[i] = '0';
    }
    /* 99..9 rounded up: 100..0 one decade higher. */
    memmove(out + 1, out, (size_t)p);
    out[0] = '1';
    out[p] = 0;
    *exp10 = e + 1;
}

/* The shortest digits of x > 0 that read back as x (f32 when is_f32): the digits without the point
 * and the decimal exponent of the first. */
static void shortest(double x, int max_digits, int is_f32, char digits[32], int *exp10) {
    char ex[1100];
    int e;
    exact(x, ex, sizeof ex, &e);
    for (int p = 1; p <= max_digits; p++) {
        int ee;
        round_half_up(ex, e, p, digits, &ee);
        char buf[64];
        snprintf(buf, sizeof buf, "%c.%se%d", digits[0], digits + 1, ee);
        if (p == 1) snprintf(buf, sizeof buf, "%ce%d", digits[0], ee);
        int same = is_f32 ? (strtof(buf, NULL) == (float)x) : (strtod(buf, NULL) == x);
        if (same || p == max_digits) {
            *exp10 = ee;
            break;
        }
    }
    size_t n = strlen(digits);
    while (n > 1 && digits[n - 1] == '0') digits[--n] = 0;
}

/* Digits d1 d2 ... dn times 10^(exp10 - n + 1), written out without an exponent. */
static char *plain(char *out, int neg, const char *digits, int exp10) {
    char *o = out;
    if (neg) *o++ = '-';
    int n = (int)strlen(digits);
    int point = exp10 + 1; /* digits before the decimal point */
    if (point <= 0) {
        *o++ = '0';
        *o++ = '.';
        for (int i = 0; i < -point; i++) *o++ = '0';
        memcpy(o, digits, (size_t)n);
        o += n;
    } else if (point >= n) {
        memcpy(o, digits, (size_t)n);
        o += n;
        for (int i = 0; i < point - n; i++) *o++ = '0';
    } else {
        memcpy(o, digits, (size_t)point);
        o += point;
        *o++ = '.';
        memcpy(o, digits + point, (size_t)(n - point));
        o += n - point;
    }
    *o = 0;
    return out;
}

static char *display(char *out, double x, int max_digits, int is_f32) {
    if (isnan(x)) return strcpy(out, "NaN");
    if (isinf(x)) return strcpy(out, x < 0 ? "-inf" : "inf");
    if (x == 0) return strcpy(out, signbit(x) ? "-0" : "0");
    char digits[32];
    int e;
    shortest(fabs(x), max_digits, is_f32, digits, &e);
    return plain(out, x < 0, digits, e);
}

char *rf_f32(char out[RF_FLOAT_MAX], float x) { return display(out, (double)x, 9, 1); }
char *rf_f64(char out[RF_FLOAT_MAX], double x) { return display(out, x, 17, 0); }

char *rf_debug_str(const char *s) {
    size_t n = strlen(s);
    char *out = malloc(n * 10 + 3);
    if (!out) return NULL;
    char *o = out;
    *o++ = '"';
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
        switch (*c) {
        case '\t': *o++ = '\\'; *o++ = 't'; break;
        case '\r': *o++ = '\\'; *o++ = 'r'; break;
        case '\n': *o++ = '\\'; *o++ = 'n'; break;
        case '\\': *o++ = '\\'; *o++ = '\\'; break;
        case '"': *o++ = '\\'; *o++ = '"'; break;
        case 0: *o++ = '\\'; *o++ = '0'; break;
        default:
            if (*c < 0x20 || *c == 0x7f) o += sprintf(o, "\\u{%x}", *c);
            else *o++ = (char)*c;
        }
    }
    *o++ = '"';
    *o = 0;
    return out;
}

char *rf_hex_alt(char out[24], uint64_t x) {
    snprintf(out, 24, "0x%llx", (unsigned long long)x);
    return out;
}
