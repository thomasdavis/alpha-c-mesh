/* Rust's float and padding formats on top of snprintf. */
#include "rsfmt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int put(char *out, size_t cap, const char *s) {
    size_t n = strlen(s);
    if (cap) {
        size_t k = n < cap - 1 ? n : cap - 1;
        memcpy(out, s, k);
        out[k] = 0;
    }
    return (int)n;
}

/* NaN and the infinities as Rust writes them; 0 when v is finite. */
static const char *special(double v, int flags) {
    if (isnan(v)) return "NaN";
    if (isinf(v)) return v < 0 ? "-inf" : (flags & RSFMT_PLUS) ? "+inf" : "inf";
    return NULL;
}

int rsfmt_fixed(char *out, size_t cap, double v, int prec, int flags) {
    const char *sp = special(v, flags);
    if (sp) return put(out, cap, sp);
    return snprintf(out, cap, (flags & RSFMT_PLUS) ? "%+.*f" : "%.*f", prec, v);
}

int rsfmt_exp(char *out, size_t cap, double v, int prec, int flags) {
    const char *sp = special(v, flags);
    if (sp) return put(out, cap, sp);
    char tmp[64];
    snprintf(tmp, sizeof tmp, (flags & RSFMT_PLUS) ? "%+.*e" : "%.*e", prec, v);
    /* "2.500e-01" -> "2.500e-1", "1.0e+21" -> "1.0e21". */
    char *e = strchr(tmp, 'e');
    char res[80];
    size_t m = (size_t)(e - tmp);
    memcpy(res, tmp, m);
    res[m++] = 'e';
    const char *x = e + 1;
    if (*x == '-') res[m++] = *x++;
    else if (*x == '+') x++;
    while (*x == '0' && x[1]) x++;
    while (*x) res[m++] = *x++;
    res[m] = 0;
    return put(out, cap, res);
}

size_t rsfmt_chars(const char *s) {
    size_t n = 0;
    for (; *s; s++) n += ((unsigned char)*s & 0xc0) != 0x80;
    return n;
}

size_t rsfmt_prefix(const char *s, size_t n) {
    size_t i = 0;
    while (s[i]) {
        if (((unsigned char)s[i] & 0xc0) != 0x80) {
            if (n == 0) break;
            n--;
        }
        i++;
    }
    return i;
}

int rsfmt_pad(char *out, size_t cap, const char *s, int width, char align, char fill) {
    size_t len = strlen(s), chars = rsfmt_chars(s);
    size_t pad = width > 0 && (size_t)width > chars ? (size_t)width - chars : 0;
    size_t pre = align == '>' ? pad : align == '^' ? pad / 2 : 0;
    size_t post = pad - pre;
    size_t total = pre + len + post, k = 0;
    if (cap) {
        for (size_t i = 0; i < pre && k + 1 < cap; i++) out[k++] = fill;
        for (size_t i = 0; i < len && k + 1 < cap; i++) out[k++] = s[i];
        for (size_t i = 0; i < post && k + 1 < cap; i++) out[k++] = fill;
        out[k] = 0;
    }
    return (int)total;
}

int rsfmt_fixed_w(char *out, size_t cap, double v, int prec, int width, char align, int flags) {
    char num[400];
    rsfmt_fixed(num, sizeof num, v, prec, flags);
    if (!(flags & RSFMT_ZERO) || isnan(v) || isinf(v)) return rsfmt_pad(out, cap, num, width, align, ' ');
    /* Zero padding goes after the sign, whatever the alignment. */
    const char *digits = num;
    char sign[2] = {0, 0};
    if (*digits == '-' || *digits == '+') sign[0] = *digits++;
    size_t len = strlen(num);
    char res[480];
    size_t k = 0, zeros = width > 0 && (size_t)width > len ? (size_t)width - len : 0;
    if (sign[0]) res[k++] = sign[0];
    for (size_t i = 0; i < zeros && k < sizeof res - 1; i++) res[k++] = '0';
    for (const char *p = digits; *p && k < sizeof res - 1; p++) res[k++] = *p;
    res[k] = 0;
    return put(out, cap, res);
}
