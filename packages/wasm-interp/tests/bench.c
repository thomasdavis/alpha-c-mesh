/* Not a check: how much slower the interpreter is than native code on the differential fixture
 * (integer and float work), printed for the README. Always passes once the results agree. */
#define _XOPEN_SOURCE 700
#include "mesh/check.h"
#include "wasm/interp.h"

#include <stdio.h>
#include <time.h>

#define PROG_PREFIX(x) native_##x
#include "fixtures/prog.c"
#include "fixtures/prog.h"

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

int main(void) {
    char err[256];
    wasm_compiled *c = wasm_compile(prog_wasm, prog_wasm_len, WASM_F_INTERP, err, sizeof err);
    CHECK(c != NULL);
    wasm_instance *I = c ? wasm_instantiate(c, NULL, 0, NULL, NULL, err, sizeof err) : NULL;
    CHECK(I != NULL);
    if (!I) return mesh_check_done();
    uint32_t fns[2] = {(uint32_t)wasm_instance_func(I, "run", "i>i", err, sizeof err),
                       (uint32_t)wasm_instance_func(I, "fp_run", "i>i", err, sizeof err)};
    const char *names[2] = {"run (integer)", "fp_run (float)"};
    for (int k = 0; k < 2; k++) {
        volatile uint32_t sink = 0;
        double t0 = now();
        int n = 0;
        while (now() - t0 < 0.3) {
            sink ^= k ? native_fp_run((uint32_t)n) : native_run((uint32_t)n);
            n++;
        }
        double native = (now() - t0) / n;
        t0 = now();
        int m = 0;
        while (now() - t0 < 0.3) {
            uint64_t io[1] = {(uint64_t)m};
            CHECK_I(wasm_call(I, fns[k], io), WASM_NUM_OK);
            sink ^= (uint32_t)io[0];
            m++;
        }
        double interp = (now() - t0) / m;
        fprintf(stderr, "bench %s: native %.1f us, interpreted %.1f us: %.1fx\n", names[k], native * 1e6, interp * 1e6,
                interp / native);
        (void)sink;
    }
    wasm_instance_free(I);
    wasm_compiled_free(c);
    return mesh_check_done();
}
