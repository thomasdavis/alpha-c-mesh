/* The encoders and the section layout of a small module. */
#include "mesh/check.h"
#include "wasm/builder.h"

#include <string.h>

int main(void) {
    wasm_buf b = {0};
    wb_u32(&b, 624485);
    CHECK_U(b.len, 3);
    CHECK_U(b.data[0], 0xe5);
    CHECK_U(b.data[2], 0x26);
    b.len = 0;
    wb_s32(&b, -123456);
    CHECK_U(b.len, 3);
    CHECK_U(b.data[0], 0xc0);
    CHECK_U(b.data[1], 0xbb);
    CHECK_U(b.data[2], 0x78);
    b.len = 0;
    wb_s64(&b, INT64_MIN);
    CHECK_U(b.len, 10);
    CHECK_U(b.data[9], 0x7f);
    b.len = 0;
    wb_s32(&b, 64);
    CHECK_U(b.len, 2);
    wasm_buf_free(&b);

    /* (module (type (func (param i32 i32) (result i32))) (func (type 0) local.get 0 local.get 1 i32.add)
     *   (export "add" (func 0))) as wat2wasm writes it. */
    static const uint8_t want[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x07, 0x01, 0x60, 0x02,
                                   0x7f, 0x7f, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00, 0x07, 0x07, 0x01, 0x03, 0x61,
                                   0x64, 0x64, 0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01,
                                   0x6a, 0x0b};
    wasm_builder w = {0};
    uint32_t t = wasm_builder_type(&w, "ii>i"), f;
    wasm_buf *body = wasm_builder_func_add(&w, t, "", &f);
    wb_op_u32(body, 0x20, 0);
    wb_op_u32(body, 0x20, 1);
    wb_op(body, 0x6a);
    wasm_builder_add_export(&w, "add", WASM_EXTERN_FUNC, f);
    wasm_buf out;
    wasm_builder_finish(&w, &out);
    CHECK_U(out.len, sizeof want);
    CHECK(out.len == sizeof want && memcmp(out.data, want, sizeof want) == 0);
    wasm_buf_free(&out);
    wasm_builder_free(&w);
    return mesh_check_done();
}
