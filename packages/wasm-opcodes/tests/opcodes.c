/* The table: every MVP opcode is known with a consistent signature, the gaps aren't, names match
 * wasmparser's, and prefixed opcodes resolve by rule. */
#include "mesh/check.h"
#include "wasm/opcodes.h"

#include <string.h>

int main(void) {
    wasm_opinfo o;
    int known = 0;
    for (uint32_t op = 0; op < 256; op++) {
        if (!wasm_op_info(op, &o)) continue;
        known++;
        CHECK_U(o.op, op);
        if (o.sig) {
            const char *gt = strchr(o.sig, '>');
            CHECK(gt != NULL);
            for (const char *c = o.sig; *c; c++) CHECK(*c == '>' || wasm_sig_type(*c) != 0);
        }
        if (o.imm == WASM_IMM_MEMARG) CHECK(o.mem_bytes == 1 || o.mem_bytes == 2 || o.mem_bytes == 4 || o.mem_bytes == 8);
    }
    /* 0x00-0x05, 0x0b-0x11, 0x1a-0x1c, 0x20-0x26, 0x28-0xc4, 0xd0-0xd2 */
    CHECK_I(known, 6 + 7 + 3 + 7 + (0xc4 - 0x28 + 1) + 3);
    CHECK(!wasm_op_info(0x06, &o));
    CHECK(!wasm_op_info(0x27, &o));
    CHECK(!wasm_op_info(0xc5, &o));
    CHECK(wasm_op_info(0x92, &o));
    CHECK_S(o.name, "F32Add");
    CHECK_S(o.sig, "ff>f");
    CHECK(wasm_op_info(0xb6, &o));
    CHECK_S(o.name, "F32DemoteF64");
    CHECK(wasm_op_info(0xbb, &o));
    CHECK_S(o.name, "F64PromoteF32");
    CHECK(wasm_op_info(WASM_OP_FC(7), &o));
    CHECK_S(o.name, "I64TruncSatF64U");
    CHECK_U(o.feature, WASM_F_SAT_FLOAT_TO_INT);
    CHECK(wasm_op_info(WASM_OP_MEMORY_COPY, &o));
    CHECK_U(o.imm, WASM_IMM_MEMORY_COPY);
    CHECK(!wasm_op_info(WASM_OP_FC(18), &o));
    /* SIMD by rule. */
    CHECK(wasm_op_info(WASM_OP_SIMD(0x0c), &o));
    CHECK_U(o.imm, WASM_IMM_V128);
    CHECK(wasm_op_info(WASM_OP_SIMD(0x0d), &o));
    CHECK_U(o.imm, WASM_IMM_SHUFFLE);
    CHECK(wasm_op_info(WASM_OP_SIMD(0x15), &o));
    CHECK_U(o.imm, WASM_IMM_LANE);
    CHECK(wasm_op_info(WASM_OP_SIMD(0x54), &o));
    CHECK_U(o.imm, WASM_IMM_MEMARG_LANE);
    CHECK(wasm_op_info(WASM_OP_SIMD(0xe4), &o));
    CHECK_U(o.imm, WASM_IMM_NONE);
    CHECK_U(o.feature, WASM_F_SIMD);
    CHECK(!wasm_op_info(WASM_OP_SIMD(0x9a), &o));
    CHECK(wasm_op_info(WASM_OP_SIMD(0x105), &o));
    CHECK_U(o.feature, WASM_F_RELAXED_SIMD);
    CHECK(!wasm_op_info(WASM_OP_SIMD(0x114), &o));
    /* Atomics by rule. */
    CHECK(wasm_op_info(WASM_OP_ATOMIC(0x03), &o));
    CHECK_U(o.imm, WASM_IMM_ZERO_BYTE);
    CHECK(wasm_op_info(WASM_OP_ATOMIC(0x4e), &o));
    CHECK_U(o.feature, WASM_F_THREADS);
    CHECK(!wasm_op_info(WASM_OP_ATOMIC(0x04), &o));
    CHECK(!wasm_op_info(WASM_OP_ATOMIC(0x4f), &o));
    CHECK(!wasm_op_info(0xff0000, &o));
    return mesh_check_done();
}
