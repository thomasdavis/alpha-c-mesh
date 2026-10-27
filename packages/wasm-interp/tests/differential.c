/* The same C, compiled natively here and to wasm32 by clang (fixtures/prog.c, its bytes in
 * fixtures/prog.h), must give the same numbers in the interpreter: integer and float arithmetic,
 * br_table, call_indirect, recursion, data segments, linear memory. */
#include "mesh/check.h"
#include "wasm/interp.h"

#include <stdio.h>

#define PROG_PREFIX(x) native_##x
#include "fixtures/prog.c"
#include "fixtures/prog.h"

static char err[256];

int main(void) {
    wasm_compiled *c = wasm_compile(prog_wasm, prog_wasm_len, WASM_F_INTERP, err, sizeof err);
    if (!c) fprintf(stderr, "compile: %s\n", err);
    CHECK(c != NULL);
    if (!c) return mesh_check_done();
    wasm_instance *I = wasm_instantiate(c, NULL, 0, NULL, NULL, err, sizeof err);
    if (!I) fprintf(stderr, "instantiate: %s\n", err);
    CHECK(I != NULL);
    if (I) {
        int64_t run = wasm_instance_func(I, "run", "i>i", err, sizeof err);
        int64_t fp = wasm_instance_func(I, "fp_run", "i>i", err, sizeof err);
        CHECK(run >= 0 && fp >= 0);
        for (uint32_t seed = 0; seed < 40 && run >= 0 && fp >= 0; seed++) {
            uint64_t io[1] = {seed};
            CHECK_I(wasm_call(I, (uint32_t)run, io), WASM_NUM_OK);
            CHECK_U((uint32_t)io[0], native_run(seed));
            io[0] = seed * 7919;
            CHECK_I(wasm_call(I, (uint32_t)fp, io), WASM_NUM_OK);
            CHECK_U((uint32_t)io[0], native_fp_run(seed * 7919));
        }
        wasm_instance_free(I);
    }
    wasm_compiled_free(c);
    return mesh_check_done();
}
