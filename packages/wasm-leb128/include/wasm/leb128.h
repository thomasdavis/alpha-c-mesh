/* @ajax/wasm-leb128: LEB128 as the WebAssembly binary format defines it (spec 5.2.2).
 *
 * Each reader decodes one number from [p, end) and returns the bytes it used, or 0 when the
 * encoding is truncated, longer than ceil(N / 7) bytes, or its last byte has bits that don't fit
 * N bits (unsigned: must be zero; signed: must repeat the sign). Freestanding, header-only. */
#ifndef WASM_LEB128_H
#define WASM_LEB128_H

#include <stddef.h>
#include <stdint.h>

/* Unsigned, at most bits (1..64) bits. */
static inline size_t wasm_leb_unsigned(const uint8_t *p, const uint8_t *end, unsigned bits, uint64_t *out) {
    uint64_t v = 0;
    unsigned shift = 0;
    size_t max = (bits + 6) / 7;
    for (size_t i = 0; i < max; i++) {
        if (p + i >= end) return 0;
        uint8_t b = p[i];
        if (i == max - 1) {
            /* The last byte may only hold the bits left. */
            unsigned left = bits - shift;
            if (b & 0x80) return 0;
            if (left < 7 && (b >> left) != 0) return 0;
        }
        v |= (uint64_t)(b & 0x7f) << shift;
        shift += 7;
        if (!(b & 0x80)) {
            *out = v;
            return i + 1;
        }
    }
    return 0;
}

/* Signed, at most bits (1..64) bits, sign-extended to 64. */
static inline size_t wasm_leb_signed(const uint8_t *p, const uint8_t *end, unsigned bits, int64_t *out) {
    uint64_t v = 0;
    unsigned shift = 0;
    size_t max = (bits + 6) / 7;
    for (size_t i = 0; i < max; i++) {
        if (p + i >= end) return 0;
        uint8_t b = p[i];
        if (i == max - 1) {
            if (b & 0x80) return 0;
            /* The unused high bits of the last byte must all equal the sign bit of the value. */
            unsigned left = bits - shift; /* 1..7 value bits in this byte */
            if (left < 7) {
                uint8_t sign = (uint8_t)((b >> (left - 1)) & 1);
                uint8_t rest = (uint8_t)((b & 0x7f) >> left);
                uint8_t want = sign ? (uint8_t)(0x7f >> left) : 0;
                if (rest != want) return 0;
            }
        }
        v |= (uint64_t)(b & 0x7f) << shift;
        shift += 7;
        if (!(b & 0x80)) {
            if (shift < 64 && (b & 0x40)) v |= ~(uint64_t)0 << shift;
            *out = (int64_t)v;
            return i + 1;
        }
    }
    return 0;
}

static inline size_t wasm_leb_u32(const uint8_t *p, const uint8_t *end, uint32_t *out) {
    uint64_t v;
    size_t n = wasm_leb_unsigned(p, end, 32, &v);
    if (n) *out = (uint32_t)v;
    return n;
}

static inline size_t wasm_leb_u64(const uint8_t *p, const uint8_t *end, uint64_t *out) {
    return wasm_leb_unsigned(p, end, 64, out);
}

static inline size_t wasm_leb_s32(const uint8_t *p, const uint8_t *end, int32_t *out) {
    int64_t v;
    size_t n = wasm_leb_signed(p, end, 32, &v);
    if (n) *out = (int32_t)v;
    return n;
}

/* Block types are s33. */
static inline size_t wasm_leb_s33(const uint8_t *p, const uint8_t *end, int64_t *out) {
    return wasm_leb_signed(p, end, 33, out);
}

static inline size_t wasm_leb_s64(const uint8_t *p, const uint8_t *end, int64_t *out) {
    return wasm_leb_signed(p, end, 64, out);
}

#endif
