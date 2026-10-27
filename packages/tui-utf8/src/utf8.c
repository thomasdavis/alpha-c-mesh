/* UTF-8 with Rust's error handling. */
#include "tui/utf8.h"

#include <stdlib.h>
#include <string.h>

uint32_t tui_utf8_next(const char *str, size_t len, size_t *i) {
    const unsigned char *s = (const unsigned char *)str;
    unsigned char b = s[*i];
    if (b < 0x80) {
        (*i)++;
        return b;
    }
    int need;
    unsigned char lo = 0x80, hi = 0xBF;
    uint32_t cp;
    if (b >= 0xC2 && b <= 0xDF) {
        need = 1;
        cp = b & 0x1F;
    } else if (b >= 0xE0 && b <= 0xEF) {
        need = 2;
        cp = b & 0x0F;
        if (b == 0xE0) lo = 0xA0;
        if (b == 0xED) hi = 0x9F;
    } else if (b >= 0xF0 && b <= 0xF4) {
        need = 3;
        cp = b & 0x07;
        if (b == 0xF0) lo = 0x90;
        if (b == 0xF4) hi = 0x8F;
    } else {
        (*i)++;
        return TUI_UTF8_REPLACEMENT;
    }
    size_t j = *i + 1;
    for (int k = 0; k < need; k++, j++) {
        unsigned char c = j < len ? s[j] : 0;
        if (j >= len || c < lo || c > hi) {
            *i = j; /* the maximal subpart ends before the offending byte */
            return TUI_UTF8_REPLACEMENT;
        }
        cp = cp << 6 | (c & 0x3F);
        lo = 0x80;
        hi = 0xBF;
    }
    *i = j;
    return cp;
}

size_t tui_utf8_encode(uint32_t cp, char out[4]) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | cp >> 12);
        out[1] = (char)(0x80 | (cp >> 6 & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | cp >> 18);
    out[1] = (char)(0x80 | (cp >> 12 & 0x3F));
    out[2] = (char)(0x80 | (cp >> 6 & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

bool tui_utf8_valid(const char *s, size_t len) {
    for (size_t i = 0; i < len;) {
        size_t at = i;
        uint32_t cp = tui_utf8_next(s, len, &i);
        if (cp == TUI_UTF8_REPLACEMENT && !(i - at == 3 && (unsigned char)s[at] == 0xEF &&
                                            (unsigned char)s[at + 1] == 0xBF && (unsigned char)s[at + 2] == 0xBD))
            return false;
    }
    return true;
}

char *tui_utf8_lossy(const void *bytes, size_t len, size_t *out_len) {
    const char *s = bytes;
    /* Each byte becomes at most 3 (a lone bad byte -> U+FFFD). */
    char *out = malloc(len * 3 + 1);
    if (!out) abort();
    size_t n = 0;
    for (size_t i = 0; i < len;) {
        size_t at = i;
        uint32_t cp = tui_utf8_next(s, len, &i);
        if (cp == TUI_UTF8_REPLACEMENT || cp >= 0x80) {
            if (cp != TUI_UTF8_REPLACEMENT || i - at != 3 || memcmp(s + at, "\xEF\xBF\xBD", 3) != 0) {
                n += tui_utf8_encode(cp, out + n);
                continue;
            }
        }
        memcpy(out + n, s + at, i - at);
        n += i - at;
    }
    out[n] = 0;
    if (out_len) *out_len = n;
    return out;
}

size_t tui_utf8_count(const char *s, size_t len) {
    size_t n = 0;
    for (size_t i = 0; i < len; i++) n += ((unsigned char)s[i] & 0xC0) != 0x80;
    return n;
}
