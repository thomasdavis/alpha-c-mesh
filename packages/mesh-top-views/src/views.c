/* crates/mesh-top/src/views.rs, ported. */
#include "mesh/top_views.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static char *dup(const char *s, size_t n) {
    char *d = malloc(n + 1);
    if (!d) abort();
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

const char *mt_vrec_get(const mt_vrec *r, const char *k) {
    for (size_t i = 0; i < r->nkv; i++)
        if (strcmp(r->kv[i].key, k) == 0) return strcmp(r->kv[i].value, "-") == 0 ? NULL : r->kv[i].value;
    return NULL;
}

const char *mt_vrec_s(const mt_vrec *r, const char *k) {
    const char *v = mt_vrec_get(r, k);
    return v ? v : "-";
}

bool mt_parse_u64(const char *s, uint64_t *out) {
    const char *p = s;
    if (*p == '+') p++;
    if (!*p) return false;
    uint64_t v = 0;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        uint64_t d = (uint64_t)(*p - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
    }
    *out = v;
    return true;
}

static bool ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return !*a && !*b;
}

bool mt_parse_f64(const char *s, double *out) {
    const char *p = s;
    bool neg = false;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    if (ieq(p, "inf") || ieq(p, "infinity")) {
        *out = neg ? -HUGE_VAL : HUGE_VAL;
        return true;
    }
    if (ieq(p, "nan")) {
        *out = (double)NAN;
        return true;
    }
    /* digits [. digits] | . digits, then an optional exponent: Rust's grammar (no hex). */
    const char *q = p;
    size_t ip = 0, fp = 0;
    while (*q >= '0' && *q <= '9') q++, ip++;
    if (*q == '.') {
        q++;
        while (*q >= '0' && *q <= '9') q++, fp++;
    }
    if (ip + fp == 0) return false;
    if (*q == 'e' || *q == 'E') {
        q++;
        if (*q == '+' || *q == '-') q++;
        if (!(*q >= '0' && *q <= '9')) return false;
        while (*q >= '0' && *q <= '9') q++;
    }
    if (*q) return false;
    errno = 0;
    *out = strtod(s, NULL);
    return true;
}

bool mt_vrec_u(const mt_vrec *r, const char *k, uint64_t *out) {
    const char *v = mt_vrec_get(r, k);
    return v && mt_parse_u64(v, out);
}

bool mt_vrec_f(const mt_vrec *r, const char *k, double *out) {
    const char *v = mt_vrec_get(r, k);
    return v && mt_parse_f64(v, out);
}

bool mt_vrec_flag(const mt_vrec *r, const char *k) {
    const char *v = mt_vrec_get(r, k);
    return v && strcmp(v, "1") == 0;
}

size_t mt_vrec_list(const mt_vrec *r, const char *k, uint32_t *out, size_t cap) {
    const char *v = mt_vrec_get(r, k);
    size_t n = 0;
    if (!v) return 0;
    while (*v && n < cap) {
        const char *c = strchr(v, ',');
        size_t len = c ? (size_t)(c - v) : strlen(v);
        char tmp[32];
        if (len < sizeof tmp) {
            memcpy(tmp, v, len);
            tmp[len] = 0;
            uint64_t x;
            if (mt_parse_u64(tmp, &x) && x <= UINT32_MAX) out[n++] = (uint32_t)x;
        }
        v += len + (c ? 1 : 0);
        if (!c) break;
    }
    return n;
}

static bool is_ws(unsigned char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

size_t mt_views_parse(const char *t, mt_vrec **out) {
    size_t n = 0, cap = 0;
    mt_vrec *recs = NULL;
    size_t len = strlen(t);
    for (size_t start = 0; start < len;) {
        const char *nl = memchr(t + start, '\n', len - start);
        size_t end = nl ? (size_t)(nl - t) : len, e = end;
        if (nl && e > start && t[e - 1] == '\r') e--;
        const char *line = t + start;
        size_t ll = e - start;
        start = nl ? end + 1 : len;
        bool blank = true;
        for (size_t i = 0; i < ll; i++)
            if (!is_ws((unsigned char)line[i])) blank = false;
        if (blank) continue;
        size_t hl = ll;
        const char *text = "";
        size_t tl = 0;
        for (size_t i = 0; i + 4 <= ll; i++)
            if (memcmp(line + i, " -- ", 4) == 0) {
                hl = i;
                text = line + i + 4;
                tl = ll - i - 4;
                break;
            }
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            recs = realloc(recs, cap * sizeof *recs);
            if (!recs) abort();
        }
        mt_vrec r = {NULL, NULL, 0, dup(text, tl)};
        size_t kvcap = 0;
        for (size_t i = 0; i < hl;) {
            while (i < hl && is_ws((unsigned char)line[i])) i++;
            if (i >= hl) break;
            size_t w = i;
            while (i < hl && !is_ws((unsigned char)line[i])) i++;
            if (!r.kind) {
                r.kind = dup(line + w, i - w);
                continue;
            }
            const char *eq = memchr(line + w, '=', i - w);
            if (r.nkv == kvcap) {
                kvcap = kvcap ? kvcap * 2 : 8;
                r.kv = realloc(r.kv, kvcap * sizeof *r.kv);
                if (!r.kv) abort();
            }
            if (eq) r.kv[r.nkv++] = (mt_kv){dup(line + w, (size_t)(eq - (line + w))), dup(eq + 1, i - (size_t)(eq - line) - 1)};
            else r.kv[r.nkv++] = (mt_kv){dup(line + w, i - w), dup("", 0)};
        }
        if (!r.kind) r.kind = dup("", 0);
        recs[n++] = r;
    }
    *out = recs;
    return n;
}

void mt_vrecs_free(mt_vrec *r, size_t n) {
    for (size_t i = 0; i < n; i++) {
        free(r[i].kind);
        free(r[i].text);
        for (size_t k = 0; k < r[i].nkv; k++) {
            free(r[i].kv[k].key);
            free(r[i].kv[k].value);
        }
        free(r[i].kv);
    }
    free(r);
}

mt_view mt_view_new(size_t machine, mt_instant at, const char *text) {
    mt_view v = {machine, at, NULL, 0, NULL};
    if (strncmp(text, "err ", 4) == 0) {
        const char *s = text + 4;
        size_t l = strlen(s);
        while (l && is_ws((unsigned char)s[l - 1])) l--;
        while (l && is_ws((unsigned char)*s)) s++, l--;
        v.err = dup(s, l);
        return v;
    }
    v.n = mt_views_parse(text, &v.recs);
    return v;
}

void mt_view_free(mt_view *v) {
    mt_vrecs_free(v->recs, v->n);
    free(v->err);
    v->recs = NULL;
    v->n = 0;
    v->err = NULL;
}

const mt_vrec *mt_view_first(const mt_view *v, const char *kind) { return mt_view_nth(v, kind, 0); }

const mt_vrec *mt_view_nth(const mt_view *v, const char *kind, size_t i) {
    for (size_t k = 0; k < v->n; k++)
        if (strcmp(v->recs[k].kind, kind) == 0 && i-- == 0) return &v->recs[k];
    return NULL;
}

size_t mt_view_count(const mt_view *v, const char *kind) {
    size_t c = 0;
    for (size_t k = 0; k < v->n; k++) c += strcmp(v->recs[k].kind, kind) == 0;
    return c;
}
