/* Spec-style checks: every trap and its wasmtime code, limits, imports, host calls, interrupts,
 * globals, tables, bulk memory, multi-value, the start function, canonical NaNs. */
#include "mesh/check.h"
#include "wasm/builder.h"
#include "wasm/interp.h"

#include <stdio.h>
#include <string.h>

static char err[256];

/* A module of one exported function "f" of sig with locals and body; memory of 1 page (max 2),
 * a table of 4 with func 0 at [1], a mutable i32 global = 5; plus extra setup by the caller. */
typedef struct {
    wasm_builder w;
    wasm_compiled *c;
    wasm_instance *I;
    int64_t f;
} T;

static wasm_buf *start(T *t, const char *sig, const char *locals) {
    memset(t, 0, sizeof *t);
    uint32_t ty = wasm_builder_type(&t->w, sig);
    wasm_builder_type(&t->w, "i>i"); /* type 1 */
    wasm_builder_type(&t->w, "i>ii"); /* type 2 */
    uint32_t f;
    wasm_buf *b = wasm_builder_func_add(&t->w, ty, locals, &f);
    wasm_builder_add_export(&t->w, "f", WASM_EXTERN_FUNC, f);
    wasm_builder_memory(&t->w, 1, 2, true);
    wasm_builder_table(&t->w, 4, 8, true);
    wb_i32_const(wasm_builder_add_global(&t->w, WASM_I32, true), 5);
    return b;
}

static bool finish(T *t, const char *sig, const wasm_host_func *imports, size_t n, const wasm_store_limits *lim) {
    wasm_buf out;
    wasm_builder_finish(&t->w, &out);
    t->c = wasm_compile(out.data, out.len, WASM_F_INTERP, err, sizeof err);
    wasm_buf_free(&out);
    if (!t->c) return false;
    t->I = wasm_instantiate(t->c, imports, n, lim, NULL, err, sizeof err);
    if (!t->I) return false;
    t->f = wasm_instance_func(t->I, "f", sig, err, sizeof err);
    return t->f >= 0;
}

static void done(T *t) {
    wasm_instance_free(t->I);
    wasm_compiled_free(t->c);
    wasm_builder_free(&t->w);
}

/* Runs body as f(sig) on args; returns the trap (or OK) and the first result in *r. */
static int run(const char *sig, const char *locals, const uint8_t *body, size_t n, uint64_t a0, uint64_t a1,
               uint64_t *r) {
    T t;
    wasm_buf *b = start(&t, sig, locals);
    wb_bytes(b, body, n);
    /* A second function for the table and call_indirect: (i32) -> i32, x + 100. */
    uint32_t g;
    wasm_buf *gb = wasm_builder_func_add(&t.w, 1, "", &g);
    wb_op_u32(gb, 0x20, 0);
    wb_i32_const(gb, 100);
    wb_op(gb, 0x6a);
    uint32_t el[] = {g};
    wasm_builder_elem(&t.w, 1, el, 1);
    wasm_builder_data(&t.w, 16, "\x01\x02\x03\x04\x05\x06\x07\x08", 8);
    int trap = -100;
    if (finish(&t, sig, NULL, 0, NULL)) {
        uint64_t io[4] = {a0, a1, 0, 0};
        trap = wasm_call(t.I, (uint32_t)t.f, io);
        *r = io[0];
    } else {
        fprintf(stderr, "setup: %s\n", err);
    }
    done(&t);
    return trap;
}

#define RUN(sig, locals, a0, a1, r, ...) \
    run(sig, locals, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), a0, a1, r)

static int host_add(wasm_instance *I, void *ctx, uint64_t *io) {
    (void)I;
    *(int *)ctx += 1;
    io[0] = (uint32_t)io[0] + (uint32_t)io[1];
    return WASM_NUM_OK;
}

static int host_trap(wasm_instance *I, void *ctx, uint64_t *io) {
    (void)I, (void)ctx, (void)io;
    return WASM_TRAP_UNREACHABLE;
}

int main(void) {
    uint64_t r = 0;
    /* Traps, with wasmtime's codes. */
    CHECK_I(RUN(">", "", 0, 0, &r, 0x00), WASM_TRAP_UNREACHABLE);
    CHECK_I(RUN("ii>i", "", 7, 0, &r, 0x20, 0, 0x20, 1, 0x6d), WASM_TRAP_INTEGER_DIVISION_BY_ZERO);
    CHECK_I(RUN("ii>i", "", 0x80000000u, 0xffffffffu, &r, 0x20, 0, 0x20, 1, 0x6d), WASM_TRAP_INTEGER_OVERFLOW);
    CHECK_I(RUN("ii>i", "", 0x80000000u, 0xffffffffu, &r, 0x20, 0, 0x20, 1, 0x6f), WASM_NUM_OK);
    CHECK_U(r, 0);
    CHECK_I(RUN("f>i", "", 0x7fc00000u, 0, &r, 0x20, 0, 0xa8), WASM_TRAP_BAD_CONVERSION_TO_INTEGER);
    CHECK_I(RUN("f>i", "", 0x4f000000u, 0, &r, 0x20, 0, 0xa8), WASM_TRAP_INTEGER_OVERFLOW); /* 2^31 */
    CHECK_I(RUN("f>i", "", 0x4f000000u, 0, &r, 0x20, 0, 0xfc, 0x00), WASM_NUM_OK);
    CHECK_U(r, 0x7fffffffu);
    /* Memory bounds: the last valid word, one past it, offset overflow past 4 GiB. */
    CHECK_I(RUN("i>i", "", 65532, 0, &r, 0x20, 0, 0x28, 2, 0), WASM_NUM_OK);
    CHECK_I(RUN("i>i", "", 65533, 0, &r, 0x20, 0, 0x28, 2, 0), WASM_TRAP_MEMORY_OUT_OF_BOUNDS);
    CHECK_I(RUN("i>i", "", 0xffffffffu, 0, &r, 0x20, 0, 0x28, 0, 0xff, 0xff, 0xff, 0xff, 0x0f), WASM_TRAP_MEMORY_OUT_OF_BOUNDS);
    CHECK_I(RUN("i>", "", 65535, 0, &r, 0x20, 0, 0x41, 1, 0x3b, 1, 0), WASM_TRAP_MEMORY_OUT_OF_BOUNDS);
    /* The data segment, little-endian loads, sign extension. */
    CHECK_I(RUN(">l", "", 0, 0, &r, 0x41, 16, 0x29, 3, 0), WASM_NUM_OK);
    CHECK_U(r, 0x0807060504030201ull);
    CHECK_I(RUN(">i", "", 0, 0, &r, 0x41, 0, 0x28, 2, 16), WASM_NUM_OK);
    CHECK_U(r, 0x04030201u);
    CHECK_I(RUN(">l", "", 0, 0, &r, 0x41, 0, 0x41, 0x80, 0x01, 0x3a, 0, 0, 0x41, 0, 0x30, 0, 0), WASM_NUM_OK);
    CHECK_U(r, 0xffffffffffffff80ull);
    /* call_indirect: a hit, a null slot, out of the table, the wrong signature. */
    CHECK_I(RUN("i>i", "", 5, 0, &r, 0x20, 0, 0x41, 1, 0x11, 1, 0), WASM_NUM_OK);
    CHECK_U(r, 105);
    CHECK_I(RUN("i>i", "", 5, 0, &r, 0x20, 0, 0x41, 0, 0x11, 1, 0), WASM_TRAP_INDIRECT_CALL_TO_NULL);
    CHECK_I(RUN("i>i", "", 5, 0, &r, 0x20, 0, 0x41, 4, 0x11, 1, 0), WASM_TRAP_TABLE_OUT_OF_BOUNDS);
    CHECK_I(RUN("i>i", "", 5, 0, &r, 0x20, 0, 0x41, 1, 0x11, 2, 0, 0x1a), WASM_TRAP_BAD_SIGNATURE);
    /* Stack overflow by unbounded recursion. */
    CHECK_I(RUN(">", "", 0, 0, &r, 0x10, 0), WASM_TRAP_STACK_OVERFLOW);
    /* memory.grow: within the max, past it (-1); memory.size. */
    CHECK_I(RUN(">i", "", 0, 0, &r, 0x41, 1, 0x40, 0, 0x1a, 0x3f, 0), WASM_NUM_OK);
    CHECK_U(r, 2);
    CHECK_I(RUN(">i", "", 0, 0, &r, 0x41, 2, 0x40, 0), WASM_NUM_OK);
    CHECK_U(r, 0xffffffffu);
    /* Globals. */
    CHECK_I(RUN(">i", "", 0, 0, &r, 0x23, 0, 0x41, 2, 0x6c, 0x24, 0, 0x23, 0), WASM_NUM_OK);
    CHECK_U(r, 10);
    /* br_table: index 0, 1, and past the end (the default). */
    for (uint32_t i = 0; i < 4; i++) {
        CHECK_I(RUN("i>i", "", i, 0, &r, 0x02, 0x40, 0x02, 0x40, 0x02, 0x40, 0x20, 0, 0x0e, 2, 0, 1, 2, 0x0b, 0x41, 10,
                    0x0f, 0x0b, 0x41, 20, 0x0f, 0x0b, 0x41, 30),
                WASM_NUM_OK);
        CHECK_U(r, i == 0 ? 10 : i == 1 ? 20 : 30);
    }
    /* A branch out of a block carrying a value, dropping the rest of the stack. */
    CHECK_I(RUN(">i", "", 0, 0, &r, 0x02, 0x7f, 0x41, 1, 0x41, 2, 0x41, 3, 0x0c, 0, 0x0b), WASM_NUM_OK);
    CHECK_U(r, 3);
    /* Multi-value block (type 2: i32 -> i32 i32), then add. */
    CHECK_I(RUN("i>i", "", 21, 0, &r, 0x20, 0, 0x02, 2, 0x20, 0, 0x0b, 0x6a), WASM_NUM_OK);
    CHECK_U(r, 42);
    /* if/else and select. */
    CHECK_I(RUN("i>i", "", 0, 0, &r, 0x20, 0, 0x04, 0x7f, 0x41, 1, 0x05, 0x41, 2, 0x0b), WASM_NUM_OK);
    CHECK_U(r, 2);
    CHECK_I(RUN("i>i", "", 3, 0, &r, 0x41, 7, 0x41, 9, 0x20, 0, 0x1b), WASM_NUM_OK);
    CHECK_U(r, 7);
    /* Loop with br_if: count to the argument. */
    CHECK_I(RUN("i>i", "i", 1000, 0, &r, 0x03, 0x40, 0x20, 1, 0x41, 1, 0x6a, 0x22, 1, 0x20, 0, 0x49, 0x0d, 0, 0x0b, 0x20, 1),
            WASM_NUM_OK);
    CHECK_U(r, 1000);
    /* Bulk memory: fill, copy (overlapping), and their bounds. */
    CHECK_I(RUN(">i", "", 0, 0, &r, 0x41, 40, 0x41, 0x2a, 0x41, 8, 0xfc, 11, 0, 0x41, 42, 0x41, 40, 0x41, 8, 0xfc, 10, 0,
                0, 0x41, 44, 0x28, 2, 0),
            WASM_NUM_OK);
    CHECK_U(r, 0x2a2a2a2au);
    CHECK_I(RUN(">", "", 0, 0, &r, 0x41, 0, 0x41, 0, 0x41, 0x80, 0x80, 0x04, 0xfc, 11, 0), WASM_NUM_OK); /* exactly 64 KiB */
    CHECK_I(RUN(">", "", 0, 0, &r, 0x41, 1, 0x41, 0, 0x41, 0x80, 0x80, 0x04, 0xfc, 11, 0), WASM_TRAP_MEMORY_OUT_OF_BOUNDS);
    /* Tables: size, grow within and past the max, get/set, fill bounds, ref.is_null. */
    CHECK_I(RUN(">i", "", 0, 0, &r, 0xfc, 16, 0), WASM_NUM_OK);
    CHECK_U(r, 4);
    CHECK_I(RUN(">i", "", 0, 0, &r, 0xd0, 0x70, 0x41, 4, 0xfc, 15, 0), WASM_NUM_OK);
    CHECK_U(r, 4);
    CHECK_I(RUN(">i", "", 0, 0, &r, 0xd0, 0x70, 0x41, 5, 0xfc, 15, 0), WASM_NUM_OK);
    CHECK_U(r, 0xffffffffu);
    CHECK_I(RUN(">i", "", 0, 0, &r, 0x41, 0, 0x25, 0, 0xd1), WASM_NUM_OK);
    CHECK_U(r, 1);
    CHECK_I(RUN(">i", "", 0, 0, &r, 0x41, 1, 0x25, 0, 0xd1), WASM_NUM_OK);
    CHECK_U(r, 0);
    CHECK_I(RUN(">", "", 0, 0, &r, 0x41, 3, 0xd0, 0x70, 0x41, 2, 0xfc, 17, 0), WASM_TRAP_TABLE_OUT_OF_BOUNDS);
    /* Canonical NaNs from arithmetic; neg keeps the payload. */
    CHECK_I(RUN("ff>f", "", 0x7fa00001u, 0x3f800000u, &r, 0x20, 0, 0x20, 1, 0x92), WASM_NUM_OK);
    CHECK_U((uint32_t)r, 0x7fc00000u);
    CHECK_I(RUN("f>f", "", 0x7fa00001u, 0, &r, 0x20, 0, 0x8c), WASM_NUM_OK);
    CHECK_U((uint32_t)r, 0xffa00001u);
    CHECK_I(RUN("d>f", "", 0x7ff0000000000001ull, 0, &r, 0x20, 0, 0xb6), WASM_NUM_OK);
    CHECK_U((uint32_t)r, 0x7fc00000u);
    CHECK_I(RUN("ll>l", "", 1, 63, &r, 0x20, 0, 0x20, 1, 0x86), WASM_NUM_OK);
    CHECK_U(r, 0x8000000000000000ull);

    /* Imports: a host function called with its args, a host trap, a missing and a mistyped import. */
    {
        T t;
        memset(&t, 0, sizeof t);
        uint32_t ty = wasm_builder_type(&t.w, "ii>i");
        uint32_t t0 = wasm_builder_type(&t.w, ">");
        wasm_builder_import_func(&t.w, "mesh", "add", ty);
        wasm_builder_import_func(&t.w, "mesh", "boom", t0);
        uint32_t f, g;
        wasm_buf *b = wasm_builder_func_add(&t.w, ty, "", &f);
        wb_op_u32(b, 0x20, 0);
        wb_op_u32(b, 0x20, 1);
        wb_op_u32(b, 0x10, 0);
        wb_i32_const(b, 1);
        wb_op(b, 0x6a);
        b = wasm_builder_func_add(&t.w, t0, "", &g);
        wb_op_u32(b, 0x10, 1);
        wasm_builder_add_export(&t.w, "f", WASM_EXTERN_FUNC, f);
        wasm_builder_add_export(&t.w, "g", WASM_EXTERN_FUNC, g);
        int calls = 0;
        wasm_host_func hosts[] = {{"mesh", "add", "ii>i", host_add, &calls}, {"mesh", "boom", ">", host_trap, NULL}};
        CHECK(finish(&t, "ii>i", hosts, 2, NULL));
        uint64_t io[2] = {40, 1};
        CHECK_I(wasm_call(t.I, (uint32_t)t.f, io), WASM_NUM_OK);
        CHECK_U(io[0], 42);
        CHECK_I(calls, 1);
        CHECK_I(wasm_call(t.I, (uint32_t)wasm_instance_func(t.I, "g", ">", err, sizeof err), io), WASM_TRAP_UNREACHABLE);
        CHECK_I(wasm_instance_func(t.I, "f", "i>i", err, sizeof err), -1);
        CHECK(strstr(err, "type mismatch") != NULL);
        wasm_instance_free(t.I);
        t.I = wasm_instantiate(t.c, hosts, 1, NULL, NULL, err, sizeof err);
        CHECK(t.I == NULL);
        CHECK_S(err, "unknown import: `mesh::boom` has not been defined");
        wasm_host_func wrong[] = {{"mesh", "add", "i>i", host_add, &calls}, {"mesh", "boom", ">", host_trap, NULL}};
        t.I = wasm_instantiate(t.c, wrong, 2, NULL, NULL, err, sizeof err);
        CHECK(t.I == NULL);
        CHECK_S(err, "incompatible import type for `mesh::add`");
        done(&t);
    }
    /* Store limits: a memory past the limit fails to instantiate; growth stops at it. */
    {
        T t;
        wasm_buf *b = start(&t, ">i", "");
        wb_i32_const(b, 1);
        wb_op(b, 0x40);
        wb_byte(b, 0);
        wasm_store_limits small = {WASM_PAGE / 2, 0, 0, 0, 0};
        CHECK(!finish(&t, ">i", NULL, 0, &small));
        CHECK_S(err, "memory minimum size of 1 pages exceeds memory limits");
        wasm_compiled_free(t.c);
        t.c = NULL;
        wasm_store_limits one = {WASM_PAGE, 0, 0, 0, 0};
        CHECK(finish(&t, ">i", NULL, 0, &one));
        uint64_t io[1];
        CHECK_I(wasm_call(t.I, (uint32_t)t.f, io), WASM_NUM_OK);
        CHECK_U(io[0], 0xffffffffu);
        done(&t);
    }
    /* Interrupts: a loop that never ends traps once the epoch reaches the deadline. */
    {
        T t;
        wasm_buf *b = start(&t, ">", "");
        wb_op(b, 0x03);
        wb_byte(b, 0x40);
        wb_op_u32(b, 0x0c, 0);
        wb_op(b, 0x0b);
        CHECK(finish(&t, ">", NULL, 0, NULL));
        static _Atomic uint64_t epoch = 5;
        wasm_set_epoch(t.I, &epoch);
        wasm_set_deadline(t.I, 5);
        uint64_t io[1];
        CHECK_I(wasm_call(t.I, (uint32_t)t.f, io), WASM_TRAP_INTERRUPT);
        CHECK_U(wasm_deadline(t.I), 5);
        done(&t);
    }
    /* A start function runs at instantiation; a trapping one fails it. */
    {
        T t;
        wasm_buf *b = start(&t, ">i", "");
        wb_op_u32(b, 0x23, 0);
        uint32_t s;
        wasm_buf *sb = wasm_builder_func_add(&t.w, wasm_builder_type(&t.w, ">"), "", &s);
        wb_i32_const(sb, 77);
        wb_op_u32(sb, 0x24, 0);
        t.w.has_start = true;
        t.w.start = s;
        CHECK(finish(&t, ">i", NULL, 0, NULL));
        uint64_t io[1];
        CHECK_I(wasm_call(t.I, (uint32_t)t.f, io), WASM_NUM_OK);
        CHECK_U(io[0], 77);
        done(&t);
        b = start(&t, ">", "");
        sb = wasm_builder_func_add(&t.w, wasm_builder_type(&t.w, ">"), "", &s);
        wb_op(sb, 0x00);
        t.w.has_start = true;
        t.w.start = s;
        CHECK(!finish(&t, ">", NULL, 0, NULL));
        CHECK_S(err, "wasm trap: wasm `unreachable` instruction executed (in the start function)");
        done(&t);
    }
    /* Not run: SIMD (refused at compile, with the executor's reason). */
    {
        T t;
        wasm_buf *b = start(&t, ">", "");
        wb_byte(b, 0xfd);
        wb_u32(b, 0x0c);
        for (int i = 0; i < 16; i++) wb_byte(b, 0);
        wb_op(b, 0x1a);
        CHECK(!finish(&t, ">", NULL, 0, NULL));
        done(&t);
        wasm_buf out;
        b = start(&t, ">", "");
        wb_byte(b, 0xfd);
        wb_u32(b, 0x0c);
        for (int i = 0; i < 16; i++) wb_byte(b, 0);
        wb_op(b, 0x1a);
        wasm_builder_finish(&t.w, &out);
        t.c = wasm_compile(out.data, out.len, WASM_F_INTERP | WASM_F_SIMD, err, sizeof err);
        CHECK(t.c == NULL);
        CHECK(strstr(err, "SIMD is not supported by this executor") != NULL);
        wasm_buf_free(&out);
        done(&t);
    }
    return mesh_check_done();
}
