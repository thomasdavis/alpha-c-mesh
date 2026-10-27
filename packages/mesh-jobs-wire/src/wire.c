/* jobs.rs's W and R. */
#include "mesh/jobs/wire.h"

#include <stdlib.h>
#include <string.h>

void mj_w_u8(msb *w, uint8_t v) { msb_put(w, &v, 1); }

void mj_w_u32(msb *w, uint32_t v) {
    uint8_t b[4];
    for (int i = 0; i < 4; i++) b[i] = (uint8_t)(v >> 8 * i);
    msb_put(w, b, 4);
}

void mj_w_u64(msb *w, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> 8 * i);
    msb_put(w, b, 8);
}

void mj_w_bytes(msb *w, const void *b, size_t n) {
    mj_w_u32(w, (uint32_t)n);
    msb_put(w, b, n);
}

void mj_w_str(msb *w, const char *s) { mj_w_bytes(w, s, strlen(s)); }

void mj_w_ranges(msb *w, const mj_ranges *r) {
    mj_w_u32(w, (uint32_t)r->n);
    for (size_t i = 0; i < r->n; i++) {
        mj_w_u64(w, r->v[i][0]);
        mj_w_u64(w, r->v[i][1]);
    }
}

void mj_rd_free(mj_rd *r) {
    free(r->err);
    r->err = NULL;
}

char *mj_rd_take_err(mj_rd *r) {
    char *e = r->err ? r->err : mstrdup("job frame: bad");
    r->err = NULL;
    return e;
}

bool mj_rd_fail(mj_rd *r, char *msg) {
    if (r->err) free(msg);
    else r->err = msg;
    return false;
}

bool mj_rd_take(mj_rd *r, size_t n, const uint8_t **p) {
    if (r->err) return false;
    if (r->n - r->at < n)
        return mj_rd_fail(r, mfmt("job frame: short (%zu of %zu bytes at %zu)", r->n - r->at, n, r->at));
    *p = r->b + r->at;
    r->at += n;
    return true;
}

bool mj_rd_u8(mj_rd *r, uint8_t *v) {
    const uint8_t *p;
    if (!mj_rd_take(r, 1, &p)) return false;
    *v = p[0];
    return true;
}

bool mj_rd_u32(mj_rd *r, uint32_t *v) {
    const uint8_t *p;
    if (!mj_rd_take(r, 4, &p)) return false;
    *v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return true;
}

bool mj_rd_u64(mj_rd *r, uint64_t *v) {
    const uint8_t *p;
    if (!mj_rd_take(r, 8, &p)) return false;
    uint64_t x = 0;
    for (int i = 7; i >= 0; i--) x = x << 8 | p[i];
    *v = x;
    return true;
}

bool mj_rd_bytes(mj_rd *r, uint8_t **b, size_t *n) {
    uint32_t len;
    const uint8_t *p;
    if (!mj_rd_u32(r, &len) || !mj_rd_take(r, len, &p)) return false;
    *b = mxalloc(len);
    memcpy(*b, p, len);
    *n = len;
    return true;
}

bool mj_utf8_ok(const uint8_t *b, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = b[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        size_t k;
        uint32_t cp, min;
        if ((c & 0xe0) == 0xc0) k = 1, cp = c & 0x1f, min = 0x80;
        else if ((c & 0xf0) == 0xe0) k = 2, cp = c & 0x0f, min = 0x800;
        else if ((c & 0xf8) == 0xf0) k = 3, cp = c & 0x07, min = 0x10000;
        else return false;
        if (n - i <= k) return false;
        for (size_t j = 1; j <= k; j++) {
            if ((b[i + j] & 0xc0) != 0x80) return false;
            cp = cp << 6 | (b[i + j] & 0x3f);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        i += k + 1;
    }
    return true;
}

/* The length of the valid UTF-8 sequence at b (1..4), or 0 with *bad the length of the maximal
 * invalid prefix to replace (Unicode's "maximal subpart", as Rust's Utf8Chunks does). */
static size_t seq_at(const uint8_t *b, size_t n, size_t *bad) {
    uint8_t c = b[0];
    if (c < 0x80) return 1;
    size_t k;
    uint8_t lo = 0x80, hi = 0xbf;
    if (c >= 0xc2 && c <= 0xdf) k = 1;
    else if (c >= 0xe0 && c <= 0xef) {
        k = 2;
        if (c == 0xe0) lo = 0xa0;
        if (c == 0xed) hi = 0x9f;
    } else if (c >= 0xf0 && c <= 0xf4) {
        k = 3;
        if (c == 0xf0) lo = 0x90;
        if (c == 0xf4) hi = 0x8f;
    } else {
        *bad = 1;
        return 0;
    }
    size_t i = 1;
    for (; i <= k && i < n; i++) {
        uint8_t x = b[i];
        if (i == 1 ? (x < lo || x > hi) : (x & 0xc0) != 0x80) break;
    }
    if (i == k + 1) return k + 1;
    *bad = i;
    return 0;
}

char *mj_utf8_lossy(const uint8_t *b, size_t n) {
    msb out = {0};
    size_t i = 0;
    while (i < n) {
        size_t bad = 0, k = seq_at(b + i, n - i, &bad);
        if (k) {
            msb_put(&out, b + i, k);
            i += k;
        } else {
            msb_puts(&out, "\xef\xbf\xbd");
            i += bad;
        }
    }
    return msb_take(&out);
}

bool mj_rd_str(mj_rd *r, char **s) {
    uint32_t len;
    const uint8_t *p;
    if (!mj_rd_u32(r, &len) || !mj_rd_take(r, len, &p)) return false;
    if (!mj_utf8_ok(p, len)) return mj_rd_fail(r, mstrdup("job frame: not UTF-8"));
    *s = mstrndup((const char *)p, len);
    return true;
}

bool mj_rd_ranges(mj_rd *r, mj_ranges *out) {
    uint32_t n;
    if (!mj_rd_u32(r, &n)) return false;
    mj_ranges x = {0};
    for (uint32_t i = 0; i < n; i++) {
        uint64_t s, e;
        if (!mj_rd_u64(r, &s) || !mj_rd_u64(r, &e)) {
            mj_ranges_free(&x);
            return false;
        }
        mj_ranges_insert(&x, s, e);
    }
    *out = x;
    return true;
}
