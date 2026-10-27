/* Instructions and immediates, and the malformed encodings that must be refused. */
#include "mesh/check.h"
#include "wasm/instr.h"

static char err[256];

static bool one(const uint8_t *b, size_t n, wasm_instr *in) {
    wasm_reader r = wasm_reader_at(b, b, b + n, err, sizeof err);
    bool ok = wasm_rd_instr(&r, in);
    if (ok) CHECK(wasm_rd_eof(&r));
    return ok;
}

#define ONE(in, ...) one((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), (in))

int main(void) {
    wasm_instr in;
    CHECK(ONE(&in, 0x41, 0x7f));
    CHECK_U(in.op, WASM_OP_I32_CONST);
    CHECK_I(in.i, -1);
    CHECK(ONE(&in, 0x43, 0x00, 0x00, 0x80, 0x3f));
    CHECK_U(in.f32, 0x3f800000u);
    CHECK(ONE(&in, 0x44, 1, 2, 3, 4, 5, 6, 7, 8));
    CHECK_U(in.f64, 0x0807060504030201ull);
    CHECK(ONE(&in, 0x28, 0x02, 0x10));
    CHECK_U(in.align, 2);
    CHECK_U(in.moffset, 16);
    CHECK_U(in.info.mem_bytes, 4);
    /* Multi-memory memarg. */
    CHECK(ONE(&in, 0x28, 0x42, 0x01, 0x08));
    CHECK_U(in.align, 2);
    CHECK_U(in.mem, 1);
    CHECK_U(in.moffset, 8);
    /* Block types. */
    CHECK(ONE(&in, 0x02, 0x40));
    CHECK_I(in.blocktype, WASM_BT_EMPTY);
    CHECK(ONE(&in, 0x02, 0x7f));
    CHECK_U(in.bt_val, WASM_I32);
    CHECK(ONE(&in, 0x02, 0x05));
    CHECK_I(in.blocktype, 5);
    CHECK(!ONE(&in, 0x02, 0x60));
    /* br_table. */
    CHECK(ONE(&in, 0x0e, 0x03, 0x00, 0x01, 0x80, 0x01, 0x02));
    uint32_t t[4];
    wasm_instr_targets(&in, t);
    CHECK_U(t[0], 0);
    CHECK_U(t[1], 1);
    CHECK_U(t[2], 128);
    CHECK_U(t[3], 2);
    CHECK(!ONE(&in, 0x0e, 0x7f, 0x00));
    /* Prefixed. */
    CHECK(ONE(&in, 0xfc, 0x0a, 0x00, 0x00));
    CHECK_U(in.op, WASM_OP_MEMORY_COPY);
    CHECK(ONE(&in, 0xfc, 0x80, 0x00));
    CHECK_U(in.op, WASM_OP_FC(0));
    CHECK(!ONE(&in, 0xfc, 0x12));
    CHECK_S(err, "unknown 0xfc subopcode: 0x12 (at offset 0x0)");
    CHECK(ONE(&in, 0xfd, 0x0c, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15));
    CHECK_U(in.v128[15], 15);
    CHECK(ONE(&in, 0xfd, 0x85, 0x02));
    CHECK_S(in.info.name, "RelaxedSimd");
    CHECK(ONE(&in, 0xfe, 0x03, 0x00));
    CHECK(!ONE(&in, 0xfe, 0x03, 0x01));
    /* Unknown, truncated. */
    CHECK(!ONE(&in, 0x06));
    CHECK_S(err, "illegal opcode: 0x6 (at offset 0x0)");
    CHECK(!ONE(&in, 0x41));
    CHECK_S(err, "unexpected end-of-file (at offset 0x1)");
    CHECK(!ONE(&in, 0x1c, 0x02, 0x7f, 0x7f));
    CHECK(ONE(&in, 0x1c, 0x01, 0x7e));
    CHECK_U(in.select_type, WASM_I64);
    CHECK(ONE(&in, 0xd0, 0x70));
    CHECK_U(in.reftype, WASM_FUNCREF);
    CHECK(!ONE(&in, 0xd0, 0x7f));
    return mesh_check_done();
}
