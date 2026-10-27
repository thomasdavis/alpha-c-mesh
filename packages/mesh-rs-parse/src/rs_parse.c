/* Rust's integer parsing (core::num, from_str_radix): sign, digits, overflow checks. */
#include "mesh/rs_parse.h"

static int digit(char c, unsigned radix) {
    unsigned d;
    if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
    else if (c >= 'a' && c <= 'z') d = (unsigned)(c - 'a') + 10;
    else if (c >= 'A' && c <= 'Z') d = (unsigned)(c - 'A') + 10;
    else return -1;
    return d < radix ? (int)d : -1;
}

bool rs_parse_uint(const char *s, size_t n, unsigned radix, uint64_t max, uint64_t *out) {
    size_t i = 0;
    if (n && s[0] == '+') i = 1;
    if (i >= n) return false;
    uint64_t v = 0;
    for (; i < n; i++) {
        int d = digit(s[i], radix);
        if (d < 0) return false;
        if (v > (max - (uint64_t)d) / radix) return false;
        v = v * radix + (uint64_t)d;
    }
    *out = v;
    return true;
}

bool rs_parse_int(const char *s, size_t n, int64_t min, int64_t max, int64_t *out) {
    size_t i = 0;
    bool neg = false;
    if (n && (s[0] == '+' || s[0] == '-')) {
        neg = s[0] == '-';
        i = 1;
    }
    if (i >= n) return false;
    /* Accumulate the magnitude as unsigned against the bound of its sign. */
    uint64_t lim = neg ? (uint64_t)(-(min + 1)) + 1 : (uint64_t)max, v = 0;
    for (; i < n; i++) {
        int d = digit(s[i], 10);
        if (d < 0) return false;
        if (v > (lim - (uint64_t)d) / 10) return false;
        v = v * 10 + (uint64_t)d;
    }
    *out = neg ? (v == 0 ? 0 : -(int64_t)(v - 1) - 1) : (int64_t)v;
    return true;
}
