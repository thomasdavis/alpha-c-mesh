/* @ajax/wasm-reader: a cursor over a WebAssembly binary. Every read is bounds-checked; the first
 * failure is recorded as "<what> (at offset 0x<n>)", wasmparser's style, and every later read
 * returns zeroes, so a parser can read straight through and check `failed` at the end.
 * Header-only. */
#ifndef WASM_READER_H
#define WASM_READER_H

#include "wasm/leb128.h"
#include "wasm/types.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t *base; /* the binary's first byte: offsets are from here */
    const uint8_t *p;
    const uint8_t *end;
    bool failed;
    char *err; /* the first failure's message, or NULL to keep none */
    size_t errlen;
} wasm_reader;

static inline wasm_reader wasm_reader_at(const uint8_t *base, const uint8_t *p, const uint8_t *end, char *err,
                                         size_t errlen) {
    wasm_reader r = {base, p, end, false, err, errlen};
    if (err && errlen) err[0] = 0;
    return r;
}

static inline size_t wasm_rd_offset(const wasm_reader *r) { return (size_t)(r->p - r->base); }
static inline bool wasm_rd_eof(const wasm_reader *r) { return r->p >= r->end; }
static inline size_t wasm_rd_left(const wasm_reader *r) { return r->p < r->end ? (size_t)(r->end - r->p) : 0; }

/* Records a failure at offset `at` (only the first one counts). Returns false. */
static inline bool wasm_rd_fail_at(wasm_reader *r, size_t at, const char *fmt, ...) {
    if (!r->failed) {
        r->failed = true;
        if (r->err && r->errlen) {
            va_list ap;
            va_start(ap, fmt);
            int n = vsnprintf(r->err, r->errlen, fmt, ap);
            va_end(ap);
            if (n >= 0 && (size_t)n < r->errlen) snprintf(r->err + n, r->errlen - (size_t)n, " (at offset 0x%zx)", at);
        }
    }
    r->p = r->end;
    return false;
}
#define wasm_rd_fail(r, ...) wasm_rd_fail_at((r), wasm_rd_offset(r), __VA_ARGS__)

static inline uint8_t wasm_rd_u8(wasm_reader *r) {
    if (r->p >= r->end) {
        wasm_rd_fail(r, "unexpected end-of-file");
        return 0;
    }
    return *r->p++;
}

static inline uint32_t wasm_rd_u32(wasm_reader *r) {
    uint32_t v = 0;
    size_t n = r->p < r->end ? wasm_leb_u32(r->p, r->end, &v) : 0;
    if (!n) {
        wasm_rd_fail(r, r->p >= r->end ? "unexpected end-of-file" : "invalid var_u32");
        return 0;
    }
    r->p += n;
    return v;
}

static inline uint64_t wasm_rd_u64(wasm_reader *r) {
    uint64_t v = 0;
    size_t n = r->p < r->end ? wasm_leb_u64(r->p, r->end, &v) : 0;
    if (!n) {
        wasm_rd_fail(r, r->p >= r->end ? "unexpected end-of-file" : "invalid var_u64");
        return 0;
    }
    r->p += n;
    return v;
}

static inline int32_t wasm_rd_s32(wasm_reader *r) {
    int32_t v = 0;
    size_t n = r->p < r->end ? wasm_leb_s32(r->p, r->end, &v) : 0;
    if (!n) {
        wasm_rd_fail(r, r->p >= r->end ? "unexpected end-of-file" : "invalid var_i32");
        return 0;
    }
    r->p += n;
    return v;
}

static inline int64_t wasm_rd_s33(wasm_reader *r) {
    int64_t v = 0;
    size_t n = r->p < r->end ? wasm_leb_s33(r->p, r->end, &v) : 0;
    if (!n) {
        wasm_rd_fail(r, r->p >= r->end ? "unexpected end-of-file" : "invalid var_s33");
        return 0;
    }
    r->p += n;
    return v;
}

static inline int64_t wasm_rd_s64(wasm_reader *r) {
    int64_t v = 0;
    size_t n = r->p < r->end ? wasm_leb_s64(r->p, r->end, &v) : 0;
    if (!n) {
        wasm_rd_fail(r, r->p >= r->end ? "unexpected end-of-file" : "invalid var_i64");
        return 0;
    }
    r->p += n;
    return v;
}

/* n raw bytes: a pointer to them, or NULL (and a failure) past the end. */
static inline const uint8_t *wasm_rd_bytes(wasm_reader *r, size_t n) {
    if (wasm_rd_left(r) < n) {
        wasm_rd_fail(r, "unexpected end-of-file");
        return NULL;
    }
    const uint8_t *s = r->p;
    r->p += n;
    return s;
}

/* Little-endian fixed-width values (float constants). */
static inline uint32_t wasm_rd_le32(wasm_reader *r) {
    const uint8_t *b = wasm_rd_bytes(r, 4);
    return b ? (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24 : 0;
}
static inline uint64_t wasm_rd_le64(wasm_reader *r) {
    uint64_t lo = wasm_rd_le32(r);
    uint64_t hi = wasm_rd_le32(r);
    return lo | hi << 32;
}

/* A string is valid UTF-8 (no overlongs, surrogates, or code points past U+10FFFF). */
static inline bool wasm_utf8_valid(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        size_t len;
        uint32_t cp, min;
        if (c < 0x80) {
            i++;
            continue;
        } else if ((c & 0xe0) == 0xc0) {
            len = 2, cp = c & 0x1f, min = 0x80;
        } else if ((c & 0xf0) == 0xe0) {
            len = 3, cp = c & 0x0f, min = 0x800;
        } else if ((c & 0xf8) == 0xf0) {
            len = 4, cp = c & 0x07, min = 0x10000;
        } else {
            return false;
        }
        if (i + len > n) return false;
        for (size_t k = 1; k < len; k++) {
            if ((s[i + k] & 0xc0) != 0x80) return false;
            cp = cp << 6 | (s[i + k] & 0x3f);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        i += len;
    }
    return true;
}

/* A name: u32 length, then that many bytes of UTF-8. */
static inline const uint8_t *wasm_rd_name(wasm_reader *r, uint32_t *len) {
    size_t at = wasm_rd_offset(r);
    uint32_t n = wasm_rd_u32(r);
    const uint8_t *s = wasm_rd_bytes(r, n);
    if (s && !wasm_utf8_valid(s, n)) {
        wasm_rd_fail_at(r, at, "malformed UTF-8 encoding");
        s = NULL;
    }
    *len = s ? n : 0;
    return s;
}

/* A vector's count, refused when it can't possibly fit in what is left (each element takes at
 * least min_bytes), so a hostile count never makes us allocate. */
static inline uint32_t wasm_rd_count(wasm_reader *r, size_t min_bytes, const char *what) {
    uint32_t n = wasm_rd_u32(r);
    if (!r->failed && min_bytes && (uint64_t)n * min_bytes > wasm_rd_left(r)) {
        wasm_rd_fail(r, "%s count is out of bounds", what);
        return 0;
    }
    return n;
}

#endif
