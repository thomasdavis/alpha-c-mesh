/* Validation: valid bodies of every kind of instruction, and the type errors, index errors and
 * disabled proposals the spec refuses. */
#include "mesh/check.h"
#include "wasm/builder.h"
#include "wasm/decode.h"
#include "wasm/validate.h"

#include <string.h>

static char err[256];

/* A module of one function of type sig with locals, whose body bytes are given; validated under f. */
static bool fn(const char *sig, const char *locals, const uint8_t *body, size_t n, wasm_features f) {
    wasm_builder w = {0};
    uint32_t t = wasm_builder_type(&w, sig);
    wasm_builder_type(&w, "i>ii"); /* type 1: for block type tests */
    wasm_buf *b = wasm_builder_func_add(&w, t, locals, NULL);
    wb_bytes(b, body, n);
    wasm_builder_memory(&w, 1, 0, false);
    wasm_builder_table(&w, 1, 0, false);
    wb_i32_const(wasm_builder_add_global(&w, WASM_I32, false), 7);
    wb_i64_const(wasm_builder_add_global(&w, WASM_I64, true), 7);
    wasm_buf out;
    wasm_builder_finish(&w, &out);
    wasm_module m;
    bool ok = wasm_decode(out.data, out.len, &m, err, sizeof err);
    if (ok) {
        ok = wasm_validate(&m, f, NULL, err, sizeof err);
        wasm_module_free(&m);
    }
    wasm_buf_free(&out);
    wasm_builder_free(&w);
    return ok;
}

#define OK(sig, locals, ...) CHECK(fn(sig, locals, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), WASM_F_ALL))
#define BAD(msg, sig, locals, ...)                                                                                   \
    do {                                                                                                             \
        CHECK(!fn(sig, locals, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), WASM_F_ALL)); \
        CHECK(strstr(err, msg) != NULL);                                                                             \
        if (!strstr(err, msg)) fprintf(stderr, "  got: %s\n", err);                                                  \
    } while (0)
#define OFF(feat, msg, sig, locals, ...)                                                                                       \
    do {                                                                                                                       \
        CHECK(!fn(sig, locals, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), WASM_F_ALL & ~(feat))); \
        CHECK(strstr(err, msg) != NULL);                                                                                       \
        if (!strstr(err, msg)) fprintf(stderr, "  got: %s\n", err);                                                            \
    } while (0)

int main(void) {
    /* Arithmetic, locals, globals, memory. */
    OK("ii>i", "", 0x20, 0, 0x20, 1, 0x6a);
    OK(">", "fd", 0x20, 0, 0x8c, 0x21, 0, 0x20, 1, 0x9f, 0x22, 1, 0x1a);
    OK(">i", "", 0x23, 0);
    OK(">", "", 0x42, 1, 0x24, 1);
    OK("i>l", "", 0x20, 0, 0x29, 3, 8);
    OK("il>", "", 0x20, 0, 0x20, 1, 0x37, 3, 0);
    OK(">i", "", 0x3f, 0x00);
    OK("i>i", "", 0x20, 0, 0x40, 0x00);
    /* Control: blocks with results, loops, if/else, br, br_if, br_table, return, unreachable. */
    OK(">i", "", 0x02, 0x7f, 0x41, 1, 0x0b);
    OK(">i", "", 0x02, 0x7f, 0x41, 1, 0x0c, 0, 0x0b);
    OK("i>i", "", 0x20, 0, 0x04, 0x7f, 0x41, 1, 0x05, 0x41, 2, 0x0b);
    OK("i>", "", 0x03, 0x40, 0x20, 0, 0x0d, 0, 0x0b);
    OK("i>i", "", 0x02, 0x40, 0x02, 0x40, 0x20, 0, 0x0e, 2, 0, 1, 0, 0x0b, 0x0b, 0x41, 0);
    OK(">i", "", 0x41, 1, 0x0f);
    OK(">i", "", 0x00);
    OK(">i", "", 0x00, 0x6a);              /* polymorphic stack after unreachable */
    OK(">l", "", 0x42, 0, 0x0c, 0, 0x41, 0, 0x1a);  /* dead code after br */
    OK("i>i", "", 0x20, 0, 0x02, 1, 0x20, 0, 0x0b, 0x6a); /* multi-value block (i32 -> i32 i32) */
    OK("ii>i", "", 0x20, 0, 0x20, 1, 0x20, 0, 0x1b);           /* select */
    OK(">", "", 0x41, 0, 0x11, 0, 0x00);                       /* call_indirect type 0 */
    OK(">i", "", 0x10, 0);                                     /* recursion */
    /* Sign extension, saturating truncation, bulk memory, reference types. */
    OK("i>i", "", 0x20, 0, 0xc0);
    OK("f>i", "", 0x20, 0, 0xfc, 0x00);
    OK(">", "", 0x41, 0, 0x41, 0, 0x41, 0, 0xfc, 0x0a, 0, 0, 0x41, 0, 0x41, 0, 0x41, 0, 0xfc, 0x0b, 0);
    OK(">i", "", 0xd0, 0x70, 0xd1);
    OK(">i", "", 0xfc, 0x10, 0);

    /* Type errors. */
    BAD("type mismatch: expected i32, found i64", "l>i", "", 0x20, 0, 0x45);
    BAD("type mismatch: operand stack empty", ">i", "", 0x6a);
    BAD("values remaining on stack at end of block", ">", "", 0x41, 1);
    BAD("type mismatch: expected i32, found f32", ">i", "", 0x43, 0, 0, 0, 0);
    BAD("else branch missing", "i>i", "", 0x20, 0, 0x04, 0x7f, 0x41, 1, 0x0b);
    BAD("type mismatch: expected i32, found i64", ">i", "", 0x02, 0x7f, 0x42, 1, 0x0c, 0, 0x0b);
    BAD("select operands have different types", "il>i", "", 0x20, 0, 0x20, 1, 0x20, 0, 0x1b);
    BAD("br_table target labels have different number of types", "i>i", "", 0x02, 0x7f, 0x02, 0x40, 0x20, 0, 0x0e, 1, 0, 1, 0x0b, 0x41, 0, 0x0b);
    /* Index errors. */
    BAD("unknown local 3: local index out of bounds", ">", "", 0x20, 3, 0x1a);
    BAD("unknown global 9: global index out of bounds", ">i", "", 0x23, 9);
    BAD("global is immutable", ">", "", 0x41, 1, 0x24, 0);
    BAD("unknown function 5: function index out of bounds", ">", "", 0x10, 5);
    BAD("unknown label: branch depth too large", ">", "", 0x0c, 1);
    BAD("unknown type: type index out of bounds", ">", "", 0x02, 0x09, 0x0b);
    BAD("alignment must not be larger than natural", "i>i", "", 0x20, 0, 0x28, 3, 0);
    BAD("undeclared function reference", ">", "", 0xd2, 0, 0x1a);
    BAD("data count section required", ">", "", 0xfc, 0x09, 0);
    /* Structure. */
    BAD("operators remaining after end of function", ">", "", 0x0b, 0x01);
    BAD("control frames remain at end of function", ">", "", 0x02, 0x40);
    BAD("else found outside of an `if` block", ">", "", 0x05);
    /* Proposals that aren't enabled. */
    OFF(WASM_F_SIGN_EXT, "sign extension operations support is not enabled", "i>i", "", 0x20, 0, 0xc0);
    OFF(WASM_F_SAT_FLOAT_TO_INT, "saturating float to int conversions support is not enabled", "f>i", "", 0x20, 0, 0xfc, 0x00);
    OFF(WASM_F_BULK_MEMORY, "bulk memory support is not enabled", ">", "", 0x41, 0, 0x41, 0, 0x41, 0, 0xfc, 0x0b, 0);
    OFF(WASM_F_REFERENCE_TYPES, "reference types support is not enabled", ">i", "", 0xd0, 0x70, 0xd1);
    OFF(WASM_F_MULTI_VALUE, "multi-value support is not enabled", "i>i", "", 0x20, 0, 0x02, 1, 0x20, 0, 0x0b, 0x6a);
    OFF(WASM_F_SIMD, "SIMD support is not enabled", ">", "", 0xfd, 0x0c, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1a);
    OFF(WASM_F_SIMD, "SIMD support is not enabled", ">", "V", 0x01);
    OFF(WASM_F_THREADS, "threads support is not enabled", ">", "", 0xfe, 0x03, 0x00);
    OFF(WASM_F_RELAXED_SIMD, "relaxed SIMD support is not enabled", ">", "", 0xfd, 0x80, 0x02, 0x1a);
    /* SIMD enabled: walked, not type-checked; but a disabled proposal after it is still found. */
    OK(">", "", 0xfd, 0x0c, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1a);
    OFF(WASM_F_THREADS, "threads support is not enabled", ">", "", 0xfd, 0x0c, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x1a, 0xfe, 0x03, 0x00);

    /* Module rules. */
    {
        wasm_builder w = {0};
        uint32_t t = wasm_builder_type(&w, ">");
        wasm_builder_func_add(&w, t, "", NULL);
        wasm_builder_add_export(&w, "a", WASM_EXTERN_FUNC, 0);
        wasm_builder_add_export(&w, "a", WASM_EXTERN_FUNC, 0);
        wasm_buf out;
        wasm_builder_finish(&w, &out);
        wasm_module m;
        CHECK(wasm_decode(out.data, out.len, &m, err, sizeof err));
        CHECK(!wasm_validate(&m, WASM_F_ALL, NULL, err, sizeof err));
        CHECK(strstr(err, "duplicate export name") != NULL);
        wasm_module_free(&m);
        wasm_buf_free(&out);
        wasm_builder_free(&w);
    }
    {
        wasm_builder w = {0};
        wasm_builder_memory(&w, 2, 1, true);
        wasm_buf out;
        wasm_builder_finish(&w, &out);
        wasm_module m;
        CHECK(wasm_decode(out.data, out.len, &m, err, sizeof err));
        CHECK(!wasm_validate(&m, WASM_F_ALL, NULL, err, sizeof err));
        CHECK(strstr(err, "size minimum must not be greater than maximum") != NULL);
        wasm_module_free(&m);
        wasm_buf_free(&out);
        wasm_builder_free(&w);
    }
    {
        /* A global initialised by a non-constant instruction. */
        wasm_builder w = {0};
        wasm_buf *g = wasm_builder_add_global(&w, WASM_I32, false);
        wb_i32_const(g, 1);
        wb_i32_const(g, 2);
        wb_op(g, 0x6a);
        wasm_buf out;
        wasm_builder_finish(&w, &out);
        wasm_module m;
        CHECK(wasm_decode(out.data, out.len, &m, err, sizeof err));
        CHECK(wasm_validate(&m, WASM_F_ALL, NULL, err, sizeof err));
        CHECK(!wasm_validate(&m, WASM_F_ALL & ~WASM_F_EXTENDED_CONST, NULL, err, sizeof err));
        CHECK(strstr(err, "extended constant expressions support is not enabled") != NULL);
        wasm_module_free(&m);
        wasm_buf_free(&out);
        wasm_builder_free(&w);
    }
    return mesh_check_done();
}
