/* The interpreter's differential fixture: compiled to wasm32 by clang (the bytes are in prog.h) and
 * natively into tests/differential.c; both must compute the same numbers. It exercises integer and
 * float arithmetic of both widths, conversions, a switch (br_table), function pointers
 * (call_indirect), recursion, a static table (a data segment), the stack and the heap of linear
 * memory, and memory.copy / memory.fill.
 *
 * Rebuild prog.h (from this folder):
 *   clang --target=wasm32 -O2 -ffp-contract=off -nostdlib -Wl,--no-entry -Wl,--export=run \
 *         -Wl,--export=fp_run -o prog.wasm prog.c && xxd -i prog.wasm > prog.h */
#include <stdint.h>

#ifndef PROG_PREFIX
#define PROG_PREFIX(x) x
#endif

static const uint32_t table[16] = {0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344, 0xa4093822, 0x299f31d0,
                                   0x082efa98, 0xec4e6c89, 0x452821e6, 0x38d01377, 0xbe5466cf, 0x34e90c6c,
                                   0xc0ac29b7, 0xc97c50dd, 0x3f84d5b5, 0xb5470917};

static uint32_t rotl(uint32_t x, int k) { return (x << (k & 31)) | (x >> ((32 - k) & 31)); }

static uint32_t op_add(uint32_t a, uint32_t b) { return a + b; }
static uint32_t op_xor(uint32_t a, uint32_t b) { return a ^ b; }
static uint32_t op_mul(uint32_t a, uint32_t b) { return a * b + 1; }
static uint32_t op_rot(uint32_t a, uint32_t b) { return rotl(a, (int)(b % 31)); }
static uint32_t (*const ops[4])(uint32_t, uint32_t) = {op_add, op_xor, op_mul, op_rot};

static uint64_t fib(uint32_t n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static uint32_t classify(uint32_t x) {
    switch (x % 9) {
    case 0: return x / 3;
    case 1: return x * 7;
    case 2: return x ^ 0xdeadbeef;
    case 3: return x >> 3;
    case 4: return (uint32_t)((int32_t)x >> 5);
    case 5: return x % 1000;
    case 6: return (uint32_t)((int32_t)x / -7);
    case 7: return (uint32_t)((int32_t)x % -13);
    default: return x + 1;
    }
}

static void fill(uint8_t *p, uint8_t v, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) p[i] = v;
}

static void copy(uint8_t *d, const uint8_t *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static uint8_t heap[4096];

uint32_t PROG_PREFIX(run)(uint32_t seed) {
    uint32_t h = seed * 2654435761u;
    uint64_t wide = 0x9e3779b97f4a7c15ull ^ seed;
    for (uint32_t i = 0; i < 2000; i++) {
        h = ops[(h >> 7) & 3](h, table[i & 15]);
        h ^= classify(h);
        wide = wide * 6364136223846793005ull + 1442695040888963407ull;
        wide ^= wide >> 29;
        h += (uint32_t)(wide >> 32) ^ (uint32_t)wide;
        if ((int64_t)wide < 0) h = rotl(h, 3);
    }
    fill(heap, (uint8_t)h, sizeof heap);
    for (uint32_t i = 0; i < 64; i++) heap[(h + i * 31) % sizeof heap] = (uint8_t)(i * h);
    copy(heap + 100, heap + 2000, 1500);
    uint32_t s = 0;
    for (uint32_t i = 0; i < sizeof heap; i++) s = s * 31 + heap[i];
    return h ^ s ^ (uint32_t)fib(20 + seed % 5);
}

/* Floats: f32 and f64 arithmetic, sqrt, conversions both ways, min/max-ish compares. */
uint32_t PROG_PREFIX(fp_run)(uint32_t seed) {
    float x = (float)(seed % 1000) / 7.0f + 0.5f;
    double y = (double)seed * 1.0e-3 - 3.25;
    uint32_t acc = 0;
    for (uint32_t i = 0; i < 500; i++) {
        x = x * 1.0009765625f + 0.333f / (x + 1.0f);
        y = y * 0.5 + __builtin_sqrt(y * y + 1.0) * 0.25 - (double)x * 1e-3; /* stays within int64 range below */
        float z = (float)y;
        double w = (double)x;
        int32_t xi = (int32_t)(x * 100.0f);
        int64_t yi = (int64_t)(y * 1e6);
        uint32_t xb, zb;
        __builtin_memcpy(&xb, &x, 4);
        __builtin_memcpy(&zb, &z, 4);
        acc = acc * 33 + xb + zb + (uint32_t)xi + (uint32_t)yi + (uint32_t)(int32_t)(w > y ? 1 : -1);
        acc ^= (uint32_t)(__builtin_sqrtf(x) * 1000.0f);
        acc += (uint32_t)(uint64_t)((double)acc / 3.0);
        acc += (uint32_t)(float)(acc >> 8);
    }
    return acc;
}
