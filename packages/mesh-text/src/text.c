/* Strings, formatting, and the Rust Debug output ported messages depend on. */
#include "mesh/text.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static _Noreturn void oom(void) {
    fputs("mesh: out of memory\n", stderr);
    abort();
}

void *mesh_alloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) oom();
    return p;
}

void *mesh_realloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) oom();
    return p;
}

char *mesh_strndup(const char *s, size_t n) {
    char *d = mesh_alloc(n + 1);
    if (n) memcpy(d, s, n);
    return d;
}

char *mesh_strdup(const char *s) { return mesh_strndup(s, strlen(s)); }

char *mesh_vfmt(const char *fmt, va_list ap) {
    mesh_text t = {0};
    mesh_text_vprintf(&t, fmt, ap);
    return mesh_text_take(&t);
}

char *mesh_fmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = mesh_vfmt(fmt, ap);
    va_end(ap);
    return s;
}

static void reserve(mesh_text *t, size_t extra) {
    if (t->len + extra + 1 <= t->cap) return;
    size_t cap = t->cap ? t->cap : 64;
    while (cap < t->len + extra + 1) cap *= 2;
    t->data = mesh_realloc(t->data, cap);
    t->cap = cap;
}

void mesh_text_putn(mesh_text *t, const char *s, size_t n) {
    reserve(t, n);
    if (n) memcpy(t->data + t->len, s, n);
    t->len += n;
    t->data[t->len] = 0;
}

void mesh_text_puts(mesh_text *t, const char *s) { mesh_text_putn(t, s, strlen(s)); }
void mesh_text_putc(mesh_text *t, char c) { mesh_text_putn(t, &c, 1); }

void mesh_text_vprintf(mesh_text *t, const char *fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) return;
    reserve(t, (size_t)n);
    vsnprintf(t->data + t->len, (size_t)n + 1, fmt, ap);
    t->len += (size_t)n;
}

void mesh_text_printf(mesh_text *t, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    mesh_text_vprintf(t, fmt, ap);
    va_end(ap);
}

const char *mesh_text_str(mesh_text *t) { return t->data ? t->data : ""; }

char *mesh_text_take(mesh_text *t) {
    reserve(t, 0);
    t->data[t->len] = 0;
    char *s = t->data;
    *t = (mesh_text){0};
    return s;
}

void mesh_text_clear(mesh_text *t) {
    t->len = 0;
    if (t->data) t->data[0] = 0;
}

void mesh_text_free(mesh_text *t) {
    free(t->data);
    *t = (mesh_text){0};
}

bool mesh_errf(char **err, const char *fmt, ...) {
    if (!err) return false;
    va_list ap;
    va_start(ap, fmt);
    char *s = mesh_vfmt(fmt, ap);
    va_end(ap);
    free(*err);
    *err = s;
    return false;
}

void mesh_err_prefix(char **err, const char *fmt, ...) {
    if (!err) return;
    mesh_text t = {0};
    va_list ap;
    va_start(ap, fmt);
    mesh_text_vprintf(&t, fmt, ap);
    va_end(ap);
    mesh_text_puts(&t, *err ? *err : "");
    free(*err);
    *err = mesh_text_take(&t);
}

/* One UTF-8 code point at s (n bytes left): its value and length; invalid
 * bytes decode as themselves with length 1 (U+FFFD-free: the caller only
 * escapes or copies). */
static size_t utf8_decode(const unsigned char *s, size_t n, uint32_t *cp) {
    unsigned char c = s[0];
    if (c < 0x80) { *cp = c; return 1; }
    size_t len = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 0;
    if (len == 0 || len > n) { *cp = c; return 1; }
    uint32_t v = c & (0x7f >> len);
    for (size_t i = 1; i < len; i++) {
        if ((s[i] & 0xc0) != 0x80) { *cp = c; return 1; }
        v = v << 6 | (s[i] & 0x3f);
    }
    *cp = v;
    return len;
}

/* Rust's escape_debug: which code points print as \u{..} (src/escapes.c). */
extern const uint32_t mesh_text_escape_ranges[][2];
extern const unsigned mesh_text_escape_ranges_len;

static bool needs_unicode_escape(uint32_t cp) {
    unsigned lo = 0, hi = mesh_text_escape_ranges_len;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (cp < mesh_text_escape_ranges[mid][0]) hi = mid;
        else if (cp > mesh_text_escape_ranges[mid][1]) lo = mid + 1;
        else return true;
    }
    return false;
}

void mesh_text_debug_str(mesh_text *t, const char *s, size_t n) {
    mesh_text_putc(t, '"');
    const unsigned char *u = (const unsigned char *)s;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        size_t len = utf8_decode(u + i, n - i, &cp);
        switch (cp) {
        case '"': mesh_text_puts(t, "\\\""); break;
        case '\\': mesh_text_puts(t, "\\\\"); break;
        case '\n': mesh_text_puts(t, "\\n"); break;
        case '\r': mesh_text_puts(t, "\\r"); break;
        case '\t': mesh_text_puts(t, "\\t"); break;
        case 0: mesh_text_puts(t, "\\0"); break;
        default:
            if (needs_unicode_escape(cp)) mesh_text_printf(t, "\\u{%x}", (unsigned)cp);
            else mesh_text_putn(t, s + i, len);
        }
        i += len;
    }
    mesh_text_putc(t, '"');
}

char *mesh_debug_str(const char *s) {
    mesh_text t = {0};
    mesh_text_debug_str(&t, s, strlen(s));
    return mesh_text_take(&t);
}

/* Shortest decimal digits that read back as v (|v| finite, > 0): the digits
 * (no dot) and the decimal exponent of the first digit. */
static void shortest(double v, int is_f32, char *digits, int *exp10) {
    char buf[64];
    for (int p = 0; p < 17; p++) {
        snprintf(buf, sizeof buf, "%.*e", p, v);
        double back = strtod(buf, NULL);
        if (is_f32 ? (float)back == (float)v : back == v) break;
    }
    /* buf: d[.ddd]e[+-]xx */
    char *e = strchr(buf, 'e');
    *exp10 = atoi(e + 1);
    size_t k = 0;
    for (char *c = buf; c < e; c++)
        if (*c >= '0' && *c <= '9') digits[k++] = *c;
    while (k > 1 && digits[k - 1] == '0') k--;
    digits[k] = 0;
}

static void debug_float(mesh_text *t, double v, int is_f32) {
    if (isnan(v)) { mesh_text_puts(t, "NaN"); return; }
    if (signbit(v)) mesh_text_putc(t, '-');
    v = fabs(v);
    if (isinf(v)) { mesh_text_puts(t, "inf"); return; }
    if (v == 0) { mesh_text_puts(t, "0.0"); return; }
    char d[40];
    int e;
    shortest(v, is_f32, d, &e);
    size_t nd = strlen(d);
    /* Rust's Debug: plain decimal for 1e-4 <= v < 1e16, else d[.ddd]e<exp>. */
    if (v >= 1e16 || v < 1e-4) {
        mesh_text_putc(t, d[0]);
        if (nd > 1) {
            mesh_text_putc(t, '.');
            mesh_text_puts(t, d + 1);
        }
        mesh_text_printf(t, "e%d", e);
        return;
    }
    if (e < 0) {
        mesh_text_puts(t, "0.");
        for (int i = 0; i < -e - 1; i++) mesh_text_putc(t, '0');
        mesh_text_puts(t, d);
    } else if ((size_t)e + 1 >= nd) {
        mesh_text_puts(t, d);
        for (size_t i = nd; i < (size_t)e + 1; i++) mesh_text_putc(t, '0');
        mesh_text_puts(t, ".0");
    } else {
        mesh_text_putn(t, d, (size_t)e + 1);
        mesh_text_putc(t, '.');
        mesh_text_puts(t, d + e + 1);
    }
}

void mesh_text_debug_f64(mesh_text *t, double v) { debug_float(t, v, 0); }
void mesh_text_debug_f32(mesh_text *t, float v) { debug_float(t, (double)v, 1); }

size_t mesh_utf8_ws_len(const char *s, size_t n) {
    if (n == 0) return 0;
    unsigned char c = (unsigned char)s[0];
    if (c == ' ' || (c >= 0x09 && c <= 0x0d)) return 1;
    if (c < 0x80) return 0;
    uint32_t cp;
    size_t len = utf8_decode((const unsigned char *)s, n, &cp);
    if (len == 1) return 0;
    bool ws = cp == 0x85 || cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 ||
              cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000;
    return ws ? len : 0;
}

void mesh_trim_start(const char **s, size_t *n) {
    size_t k;
    while ((k = mesh_utf8_ws_len(*s, *n)) > 0) {
        *s += k;
        *n -= k;
    }
}

void mesh_trim_end(const char **s, size_t *n) {
    while (*n > 0) {
        /* The last code point starts at most 3 continuation bytes back. */
        size_t start = *n - 1;
        while (start > 0 && *n - start < 4 && ((unsigned char)(*s)[start] & 0xc0) == 0x80) start--;
        if (mesh_utf8_ws_len(*s + start, *n - start) != *n - start) return;
        *n = start;
    }
}

void mesh_trim(const char **s, size_t *n) {
    mesh_trim_start(s, n);
    mesh_trim_end(s, n);
}

bool mesh_parse_u64(const char *s, size_t n, unsigned radix, uint64_t *out) {
    size_t i = 0;
    if (n > 0 && s[0] == '+') i = 1;
    if (i == n) return false;
    uint64_t v = 0;
    for (; i < n; i++) {
        unsigned c = (unsigned char)s[i], d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else return false;
        if (d >= radix) return false;
        if (v > (UINT64_MAX - d) / radix) return false;
        v = v * radix + d;
    }
    *out = v;
    return true;
}

bool mesh_parse_u32(const char *s, size_t n, unsigned radix, uint32_t *out) {
    uint64_t v;
    if (!mesh_parse_u64(s, n, radix, &v) || v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static bool ieq(const char *s, size_t n, const char *w) { return strlen(w) == n && strncasecmp(s, w, n) == 0; }

bool mesh_parse_f64(const char *s, size_t n, double *out) {
    size_t i = 0;
    bool neg = false;
    if (i < n && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    const char *r = s + i;
    size_t rn = n - i;
    if (ieq(r, rn, "inf") || ieq(r, rn, "infinity")) { *out = neg ? -HUGE_VAL : HUGE_VAL; return true; }
    if (ieq(r, rn, "nan")) { *out = NAN; return true; }
    size_t int_digits = 0, frac_digits = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') { i++; int_digits++; }
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') { i++; frac_digits++; }
    }
    if (int_digits + frac_digits == 0) return false;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) i++;
        size_t ed = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') { i++; ed++; }
        if (ed == 0) return false;
    }
    if (i != n) return false;
    char *tmp = mesh_strndup(s, n);
    errno = 0;
    *out = strtod(tmp, NULL);
    free(tmp);
    return true;
}
