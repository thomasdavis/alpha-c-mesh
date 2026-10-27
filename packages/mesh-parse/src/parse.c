/* Rust's integer and float FromStr, in C. */
#include "mesh/parse.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool digits_u64(const char *s, unsigned base, uint64_t max, uint64_t *out) {
    if (*s == '+') s++;
    if (!*s) return false;
    uint64_t v = 0;
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = (unsigned)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
        else if (base == 16 && *s >= 'A' && *s <= 'F') d = (unsigned)(*s - 'A' + 10);
        else return false;
        if (v > (max - d) / base) return false;
        v = v * base + d;
    }
    *out = v;
    return true;
}

bool mp_u64(const char *s, uint64_t *out) { return digits_u64(s, 10, UINT64_MAX, out); }
bool mp_hex_u64(const char *s, uint64_t *out) { return digits_u64(s, 16, UINT64_MAX, out); }

bool mp_u32(const char *s, uint32_t *out) {
    uint64_t v;
    if (!digits_u64(s, 10, UINT32_MAX, &v)) return false;
    *out = (uint32_t)v;
    return true;
}

bool mp_u16(const char *s, uint16_t *out) {
    uint64_t v;
    if (!digits_u64(s, 10, UINT16_MAX, &v)) return false;
    *out = (uint16_t)v;
    return true;
}

bool mp_hex_u32(const char *s, uint32_t *out) {
    uint64_t v;
    if (!digits_u64(s, 16, UINT32_MAX, &v)) return false;
    *out = (uint32_t)v;
    return true;
}

bool mp_i32(const char *s, int32_t *out) {
    bool neg = *s == '-';
    uint64_t v;
    if (!digits_u64(neg ? s + 1 : s, 10, neg ? (uint64_t)INT32_MAX + 1 : INT32_MAX, &v)) return false;
    if (neg && s[1] == '+') return false;
    *out = neg ? (int32_t)(-(int64_t)v) : (int32_t)v;
    return true;
}

static bool float_syntax(const char *s) {
    const char *p = s;
    if (*p == '+' || *p == '-') p++;
    if (strcasecmp(p, "inf") == 0 || strcasecmp(p, "infinity") == 0 || strcasecmp(p, "nan") == 0) return true;
    int whole = 0, frac = 0;
    while (*p >= '0' && *p <= '9') p++, whole++;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') p++, frac++;
    }
    if (!whole && !frac) return false;
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        int ex = 0;
        while (*p >= '0' && *p <= '9') p++, ex++;
        if (!ex) return false;
    }
    return *p == 0;
}

bool mp_f64(const char *s, double *out) {
    if (!float_syntax(s)) return false;
    *out = strtod(s, NULL);
    return true;
}

bool mp_f32(const char *s, float *out) {
    if (!float_syntax(s)) return false;
    *out = strtof(s, NULL);
    return true;
}
