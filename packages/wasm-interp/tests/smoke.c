/* A few functions end to end: arithmetic, a loop, a call, memory, a trap. */
#include "mesh/check.h"
#include "wasm/builder.h"
#include "wasm/interp.h"

#include <stdio.h>

static char err[256];

int main(void) {
    wasm_builder w = {0};
    uint32_t t_ii_i = wasm_builder_type(&w, "ii>i");
    uint32_t t_i_i = wasm_builder_type(&w, "i>i");
    uint32_t add, fact, sum;
    wasm_buf *b = wasm_builder_func_add(&w, t_ii_i, "", &add);
    wb_op_u32(b, 0x20, 0);
    wb_op_u32(b, 0x20, 1);
    wb_op(b, 0x6a);
    /* fact(n): n <= 1 ? 1 : n * fact(n - 1) */
    b = wasm_builder_func_add(&w, t_i_i, "", &fact);
    wb_op_u32(b, 0x20, 0);
    wb_i32_const(b, 1);
    wb_op(b, 0x4c); /* le_s */
    wb_op(b, 0x04);
    wb_byte(b, 0x7f);
    wb_i32_const(b, 1);
    wb_op(b, 0x05);
    wb_op_u32(b, 0x20, 0);
    wb_op_u32(b, 0x20, 0);
    wb_i32_const(b, 1);
    wb_op(b, 0x6b);
    wb_op_u32(b, 0x10, 1);
    wb_op(b, 0x6c);
    wb_op(b, 0x0b);
    /* sum(n): s = 0; i = 0; loop { s += i; i++; br_if i < n } ; store s at 8; load */
    b = wasm_builder_func_add(&w, t_i_i, "ii", &sum);
    wb_op(b, 0x03);
    wb_byte(b, 0x40);
    wb_op_u32(b, 0x20, 1);
    wb_op_u32(b, 0x20, 2);
    wb_op(b, 0x6a);
    wb_op_u32(b, 0x21, 1);
    wb_op_u32(b, 0x20, 2);
    wb_i32_const(b, 1);
    wb_op(b, 0x6a);
    wb_op_u32(b, 0x22, 2);
    wb_op_u32(b, 0x20, 0);
    wb_op(b, 0x48);
    wb_op_u32(b, 0x0d, 0);
    wb_op(b, 0x0b);
    wb_i32_const(b, 8);
    wb_op_u32(b, 0x20, 1);
    wb_memarg(b, 0x36, 2, 0);
    wb_i32_const(b, 8);
    wb_memarg(b, 0x28, 2, 0);
    wasm_builder_memory(&w, 1, 0, false);
    wasm_builder_add_export(&w, "add", WASM_EXTERN_FUNC, add);
    wasm_builder_add_export(&w, "fact", WASM_EXTERN_FUNC, fact);
    wasm_builder_add_export(&w, "sum", WASM_EXTERN_FUNC, sum);
    wasm_buf out;
    wasm_builder_finish(&w, &out);
    wasm_compiled *c = wasm_compile(out.data, out.len, WASM_F_INTERP, err, sizeof err);
    if (!c) fprintf(stderr, "compile: %s\n", err);
    CHECK(c != NULL);
    wasm_instance *I = c ? wasm_instantiate(c, NULL, 0, NULL, NULL, err, sizeof err) : NULL;
    if (c && !I) fprintf(stderr, "instantiate: %s\n", err);
    CHECK(I != NULL);
    if (I) {
        uint64_t io[2] = {40, 2};
        CHECK_I(wasm_call(I, (uint32_t)wasm_instance_func(I, "add", "ii>i", err, sizeof err), io), WASM_NUM_OK);
        CHECK_U(io[0], 42);
        io[0] = 10;
        CHECK_I(wasm_call(I, (uint32_t)wasm_instance_func(I, "fact", "i>i", err, sizeof err), io), WASM_NUM_OK);
        CHECK_U(io[0], 3628800);
        io[0] = 100;
        CHECK_I(wasm_call(I, (uint32_t)wasm_instance_func(I, "sum", "i>i", err, sizeof err), io), WASM_NUM_OK);
        CHECK_U(io[0], 4950);
        io[0] = 100000;
        CHECK_I(wasm_call(I, (uint32_t)wasm_instance_func(I, "fact", "i>i", err, sizeof err), io), WASM_TRAP_STACK_OVERFLOW);
        CHECK_I(wasm_instance_func(I, "nope", "i>i", err, sizeof err), -1);
        CHECK_S(err, "failed to find function export `nope`");
        wasm_instance_free(I);
    }
    wasm_compiled_free(c);
    wasm_buf_free(&out);
    wasm_builder_free(&w);
    return mesh_check_done();
}
