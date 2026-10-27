/* LEB128: values, the length limits and the padding rules of the WebAssembly spec. */
#include "mesh/check.h"
#include "wasm/leb128.h"

#define BYTES(...) ((const uint8_t[]){__VA_ARGS__})
#define LEN(...) sizeof((const uint8_t[]){__VA_ARGS__})

static size_t u32(const uint8_t *p, size_t n, uint32_t *v) { return wasm_leb_u32(p, p + n, v); }
static size_t s32(const uint8_t *p, size_t n, int32_t *v) { return wasm_leb_s32(p, p + n, v); }
static size_t s33(const uint8_t *p, size_t n, int64_t *v) { return wasm_leb_s33(p, p + n, v); }
static size_t s64(const uint8_t *p, size_t n, int64_t *v) { return wasm_leb_s64(p, p + n, v); }
static size_t u64(const uint8_t *p, size_t n, uint64_t *v) { return wasm_leb_u64(p, p + n, v); }

int main(void) {
    uint32_t a;
    int32_t b;
    int64_t c;
    uint64_t d;
    CHECK_U(u32(BYTES(0x00), 1, &a), 1);
    CHECK_U(a, 0);
    CHECK_U(u32(BYTES(0xe5, 0x8e, 0x26), 3, &a), 3);
    CHECK_U(a, 624485);
    /* Non-minimal encodings are fine within the length limit. */
    CHECK_U(u32(BYTES(0x80, 0x80, 0x80, 0x80, 0x00), 5, &a), 5);
    CHECK_U(a, 0);
    CHECK_U(u32(BYTES(0xff, 0xff, 0xff, 0xff, 0x0f), 5, &a), 5);
    CHECK_U(a, 0xffffffffu);
    /* Too long, unused bits set, truncated. */
    CHECK_U(u32(BYTES(0x80, 0x80, 0x80, 0x80, 0x80, 0x00), 6, &a), 0);
    CHECK_U(u32(BYTES(0xff, 0xff, 0xff, 0xff, 0x1f), 5, &a), 0);
    CHECK_U(u32(BYTES(0x80, 0x80), 2, &a), 0);
    CHECK_U(u32(BYTES(0x80), 0, &a), 0);

    CHECK_U(s32(BYTES(0x7f), 1, &b), 1);
    CHECK_I(b, -1);
    CHECK_U(s32(BYTES(0xc0, 0xbb, 0x78), 3, &b), 3);
    CHECK_I(b, -123456);
    CHECK_U(s32(BYTES(0x80, 0x80, 0x80, 0x80, 0x78), 5, &b), 5);
    CHECK_I(b, INT32_MIN);
    CHECK_U(s32(BYTES(0xff, 0xff, 0xff, 0xff, 0x07), 5, &b), 5);
    CHECK_I(b, INT32_MAX);
    /* The last byte's unused bits must repeat the sign. */
    CHECK_U(s32(BYTES(0xff, 0xff, 0xff, 0xff, 0x0f), 5, &b), 0);
    CHECK_U(s32(BYTES(0x80, 0x80, 0x80, 0x80, 0x70), 5, &b), 0);
    CHECK_U(s32(BYTES(0xff, 0xff, 0xff, 0xff, 0x7f), 5, &b), 5);
    CHECK_I(b, -1);

    CHECK_U(s33(BYTES(0x40), 1, &c), 1);
    CHECK_I(c, -64);
    CHECK_U(s33(BYTES(0xff, 0xff, 0xff, 0xff, 0x0f), 5, &c), 5);
    CHECK_I(c, 0xffffffffll);
    CHECK_U(s33(BYTES(0xff, 0xff, 0xff, 0xff, 0x7f), 5, &c), 5);
    CHECK_I(c, -1);
    CHECK_U(s33(BYTES(0xff, 0xff, 0xff, 0xff, 0x1f), 5, &c), 0);
    CHECK_U(s33(BYTES(0xff, 0xff, 0xff, 0xff, 0x2f), 5, &c), 0);

    CHECK_U(s64(BYTES(0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f), 10, &c), 10);
    CHECK_I(c, INT64_MIN);
    CHECK_U(s64(BYTES(0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00), 10, &c), 10);
    CHECK_I(c, INT64_MAX);
    CHECK_U(s64(BYTES(0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01), 10, &c), 0);
    CHECK_U(u64(BYTES(0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01), 10, &d), 10);
    CHECK_U(d, UINT64_MAX);
    CHECK_U(u64(BYTES(0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02), 10, &d), 0);

    /* Round trips of every 32-bit edge. */
    const uint32_t vals[] = {0, 1, 63, 64, 127, 128, 8191, 8192, 0x7fffffff, 0x80000000u, 0xffffffffu};
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        uint8_t buf[5];
        size_t n = 0;
        uint32_t v = vals[i];
        do {
            uint8_t byte = v & 0x7f;
            v >>= 7;
            buf[n++] = (uint8_t)(byte | (v ? 0x80 : 0));
        } while (v);
        CHECK_U(u32(buf, n, &a), n);
        CHECK_U(a, vals[i]);
        int64_t s = (int32_t)vals[i];
        n = 0;
        for (;;) {
            uint8_t byte = (uint8_t)(s & 0x7f);
            s >>= 7; /* arithmetic on every target we build for */
            if ((s == 0 && !(byte & 0x40)) || (s == -1 && (byte & 0x40))) {
                buf[n++] = byte;
                break;
            }
            buf[n++] = (uint8_t)(byte | 0x80);
        }
        CHECK_U(s32(buf, n, &b), n);
        CHECK_I(b, (int32_t)vals[i]);
    }
    return mesh_check_done();
}
