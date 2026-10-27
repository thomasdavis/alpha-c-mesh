/* Decoding: a module of every section kind, and the malformed binaries the format forbids. */
#include "mesh/check.h"
#include "wasm/builder.h"
#include "wasm/decode.h"

#include <stdlib.h>
#include <string.h>

static char err[256];

static bool dec(const uint8_t *b, size_t n, wasm_module *m) { return wasm_decode(b, n, m, err, sizeof err); }

#define BIN(...) (const uint8_t[]){0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, __VA_ARGS__}
#define NBIN(...) (8 + sizeof((const uint8_t[]){__VA_ARGS__}))
#define BAD(msg, ...)                                                                          \
    do {                                                                                       \
        wasm_module m_;                                                                        \
        CHECK(!dec(BIN(__VA_ARGS__), NBIN(__VA_ARGS__), &m_));                                 \
        CHECK_S(err, msg);                                                                     \
    } while (0)

static void full(void) {
    wasm_builder w = {0};
    uint32_t t0 = wasm_builder_type(&w, "ii>i");
    uint32_t t1 = wasm_builder_type(&w, ">");
    uint32_t imp = wasm_builder_import_func(&w, "mesh", "info", t0);
    CHECK_U(imp, 0);
    uint32_t f;
    wasm_buf *b = wasm_builder_func_add(&w, t0, "iil", &f);
    wb_op_u32(b, 0x20, 0);
    wb_op_u32(b, 0x20, 1);
    wb_op(b, 0x6a);
    wasm_builder_func_add(&w, t1, "", NULL);
    wasm_builder_memory(&w, 1, 2, true);
    wasm_builder_table(&w, 2, 0, false);
    wb_i32_const(wasm_builder_add_global(&w, WASM_I32, true), 42);
    wasm_builder_add_export(&w, "add", WASM_EXTERN_FUNC, f);
    wasm_builder_add_export(&w, "memory", WASM_EXTERN_MEMORY, 0);
    uint32_t el[] = {1, 2};
    wasm_builder_elem(&w, 0, el, 2);
    wasm_builder_data(&w, 16, "hello", 5);
    wasm_builder_data_passive(&w, "xy", 2);
    wasm_buf out;
    wasm_builder_finish(&w, &out);
    wasm_module m;
    CHECK(dec(out.data, out.len, &m));
    CHECK_U(m.ntypes, 2);
    CHECK_U(m.types[0].nparams, 2);
    CHECK_U(m.types[0].results[0], WASM_I32);
    CHECK_U(m.nimports, 1);
    CHECK_U(m.imports[0].name_len, 4);
    CHECK(memcmp(m.imports[0].name, "info", 4) == 0);
    CHECK_U(m.nfuncs, 3);
    CHECK_U(m.nfunc_imports, 1);
    CHECK_U(m.funcs[2], 1);
    CHECK_U(m.ncodes, 2);
    CHECK_U(m.codes[0].nlocals, 3);
    CHECK_U(m.codes[0].nlocal_groups, 2);
    CHECK_U(m.codes[0].body.start[m.codes[0].body.len - 1], 0x0b);
    CHECK_U(m.nmems, 1);
    CHECK_U(m.mems[0].min, 1);
    CHECK_U(m.mems[0].max, 2);
    CHECK_U(m.ntables, 1);
    CHECK_U(m.tables[0].elem, WASM_FUNCREF);
    CHECK_U(m.nglobals, 1);
    CHECK(m.globals[0].mut);
    CHECK_U(m.global_inits[0].len, 3);
    CHECK(wasm_module_export(&m, "add", WASM_EXTERN_FUNC) != NULL);
    CHECK(wasm_module_export(&m, "add", WASM_EXTERN_MEMORY) == NULL);
    CHECK_U(m.nelems, 1);
    CHECK_U(m.elems[0].count, 2);
    CHECK_U(m.elems[0].funcs[1], 2);
    CHECK_U(m.ndatas, 2);
    CHECK_U(m.datas[0].len, 5);
    CHECK(memcmp(m.datas[0].bytes, "hello", 5) == 0);
    CHECK_U(m.datas[1].mode, WASM_SEG_PASSIVE);
    CHECK(m.has_datacount);
    wasm_module_free(&m);

    /* Every truncation of a valid module is refused, without reading past it. */
    for (size_t n = 0; n < out.len; n++) {
        uint8_t *copy = malloc(n ? n : 1);
        memcpy(copy, out.data, n);
        /* A cut at a section boundary can be a valid (smaller) module; anywhere else it can't. */
        if (dec(copy, n, &m)) {
            CHECK(n == 8 || m.ncodes == 0);
            wasm_module_free(&m);
        }
        free(copy);
    }
    wasm_buf_free(&out);
    wasm_builder_free(&w);
}

int main(void) {
    full();
    wasm_module m;
    CHECK(dec((const uint8_t[]){0x00, 0x61, 0x73, 0x6d, 1, 0, 0, 0}, 8, &m));
    CHECK_U(m.nfuncs, 0);
    wasm_module_free(&m);
    CHECK(!dec((const uint8_t[]){0x00, 0x61, 0x73, 0x6e, 1, 0, 0, 0}, 8, &m));
    CHECK_S(err, "magic header not detected: bad magic number (at offset 0x0)");
    CHECK(!dec((const uint8_t[]){0x00, 0x61, 0x73, 0x6d, 2, 0, 0, 0}, 8, &m));
    CHECK_S(err, "unknown binary version: 0x2 (at offset 0x4)");
    /* Order, ids, sizes. */
    BAD("section out of order (at offset 0xb)", 0x03, 0x01, 0x00, 0x01, 0x01, 0x00);
    BAD("malformed section id: 14 (at offset 0x8)", 0x0e, 0x00);
    BAD("section size mismatch: unexpected data at the end of the section (at offset 0xb)", 0x01, 0x02, 0x00, 0x00);
    BAD("unexpected end-of-file (at offset 0xa)", 0x01, 0x05, 0x00);
    /* A custom section anywhere, and its name must be UTF-8. */
    CHECK(dec(BIN(0x00, 0x03, 0x01, 'x', 0xff, 0x01, 0x01, 0x00), NBIN(0x00, 0x03, 0x01, 'x', 0xff, 0x01, 0x01, 0x00), &m));
    wasm_module_free(&m);
    BAD("malformed UTF-8 encoding (at offset 0xa)", 0x00, 0x02, 0x01, 0xff);
    /* Types. */
    BAD("invalid leading byte in type definition (0x61) (at offset 0xb)", 0x01, 0x04, 0x01, 0x61, 0x00, 0x00);
    BAD("invalid value type (at offset 0xd)", 0x01, 0x05, 0x01, 0x60, 0x01, 0x00, 0x00);
    /* A huge count can't allocate. */
    BAD("types count is out of bounds (at offset 0xf)", 0x01, 0x05, 0xff, 0xff, 0xff, 0xff, 0x0f);
    /* Functions and code must agree. */
    BAD("function and code section have inconsistent lengths (at offset 0x14)", 0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
        0x03, 0x02, 0x01, 0x00, 0x0a, 0x01, 0x00);
    BAD("function and code section have inconsistent lengths (at offset 0x12)", 0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
        0x03, 0x02, 0x01, 0x00);
    BAD("too many locals (at offset 0x19)", 0x01, 0x04, 0x01, 0x60, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x0a, 0x0a,
        0x01, 0x08, 0x02, 0x01, 0x7f, 0x80, 0x87, 0x03, 0x7f, 0x0b);
    /* Limits and kinds. */
    BAD("invalid memory limits flags (at offset 0xb)", 0x05, 0x03, 0x01, 0x08, 0x00);
    BAD("invalid table resizable limits flags (at offset 0xc)", 0x04, 0x04, 0x01, 0x70, 0x02, 0x00);
    BAD("malformed reference type (at offset 0xb)", 0x04, 0x04, 0x01, 0x7f, 0x00, 0x00);
    BAD("invalid leading byte (0x5) for external kind (at offset 0xf)", 0x02, 0x07, 0x01, 0x01, 'm', 0x01, 'n', 0x05,
        0x00);
    BAD("malformed mutability (at offset 0xc)", 0x06, 0x06, 0x01, 0x7f, 0x02, 0x41, 0x00, 0x0b);
    BAD("invalid flags byte in data segment (at offset 0xb)", 0x0b, 0x03, 0x01, 0x03, 0x00);
    BAD("data count and data section have inconsistent lengths (at offset 0xd)", 0x0c, 0x01, 0x02, 0x0b, 0x01, 0x00);
    BAD("data count and data section have inconsistent lengths (at offset 0xb)", 0x0c, 0x01, 0x01);
    BAD("exceptions proposal not enabled (at offset 0x8)", 0x0d, 0x00);
    /* A constant expression must be well-formed instructions ending in end. */
    BAD("illegal opcode: 0x6 (at offset 0xd)", 0x06, 0x05, 0x01, 0x7f, 0x00, 0x06, 0x0b);
    return mesh_check_done();
}
