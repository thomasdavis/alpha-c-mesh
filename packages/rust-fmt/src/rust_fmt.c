/* Rust's float Display and string Debug, in C (see the header for why printf is not enough). */
#include "mesh/rust_fmt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The shortest scientific digits of x that read back as x ("d.ddde±N" from printf with the least
 * precision that round-trips): the digits without the point, and the decimal exponent. */
static void shortest(double x, int max_digits, int is_f32, char digits[32], int *exp10) {
    char buf[64];
    for (int p = 1; p <= max_digits; p++) {
        snprintf(buf, sizeof buf, "%.*e", p - 1, x);
        int same = is_f32 ? (strtof(buf, NULL) == (float)x) : (strtod(buf, NULL) == x);
        if (same || p == max_digits) break;
    }
    /* buf is "d[.ddd]e[+-]NN" (x > 0 here). */
    int n = 0;
    const char *c = buf;
    for (; *c && *c != 'e'; c++)
        if (*c >= '0' && *c <= '9') digits[n++] = *c;
    digits[n] = 0;
    *exp10 = atoi(c + 1);
    /* printf may keep trailing zeros of the precision it was asked for. */
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
