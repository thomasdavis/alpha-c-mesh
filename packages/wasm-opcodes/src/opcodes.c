/* The instruction table: the MVP and the proposals we run as rows; SIMD and atomics by rule
 * (their immediates only). */
#include "wasm/opcodes.h"

#include <stddef.h>

#define R(code, nm, im, feat, sg, mb) [code] = {code, nm, im, feat, sg, mb}
#define N(code, nm, sg) R(code, nm, WASM_IMM_NONE, 0, sg, 0)
#define L(code, nm, sg, mb) R(code, nm, WASM_IMM_MEMARG, 0, sg, mb)
#define REF WASM_F_REFERENCE_TYPES
#define BULK WASM_F_BULK_MEMORY
#define SAT WASM_F_SAT_FLOAT_TO_INT

static const wasm_opinfo main_ops[256] = {
    R(0x00, "Unreachable", WASM_IMM_NONE, 0, NULL, 0),
    R(0x01, "Nop", WASM_IMM_NONE, 0, NULL, 0),
    R(0x02, "Block", WASM_IMM_BLOCK, 0, NULL, 0),
    R(0x03, "Loop", WASM_IMM_BLOCK, 0, NULL, 0),
    R(0x04, "If", WASM_IMM_BLOCK, 0, NULL, 0),
    R(0x05, "Else", WASM_IMM_NONE, 0, NULL, 0),
    R(0x0b, "End", WASM_IMM_NONE, 0, NULL, 0),
    R(0x0c, "Br", WASM_IMM_LABEL, 0, NULL, 0),
    R(0x0d, "BrIf", WASM_IMM_LABEL, 0, NULL, 0),
    R(0x0e, "BrTable", WASM_IMM_BR_TABLE, 0, NULL, 0),
    R(0x0f, "Return", WASM_IMM_NONE, 0, NULL, 0),
    R(0x10, "Call", WASM_IMM_FUNC, 0, NULL, 0),
    R(0x11, "CallIndirect", WASM_IMM_CALL_INDIRECT, 0, NULL, 0),
    R(0x1a, "Drop", WASM_IMM_NONE, 0, NULL, 0),
    R(0x1b, "Select", WASM_IMM_NONE, 0, NULL, 0),
    R(0x1c, "TypedSelect", WASM_IMM_SELECT_T, REF, NULL, 0),
    R(0x20, "LocalGet", WASM_IMM_LOCAL, 0, NULL, 0),
    R(0x21, "LocalSet", WASM_IMM_LOCAL, 0, NULL, 0),
    R(0x22, "LocalTee", WASM_IMM_LOCAL, 0, NULL, 0),
    R(0x23, "GlobalGet", WASM_IMM_GLOBAL, 0, NULL, 0),
    R(0x24, "GlobalSet", WASM_IMM_GLOBAL, 0, NULL, 0),
    R(0x25, "TableGet", WASM_IMM_TABLE, REF, NULL, 0),
    R(0x26, "TableSet", WASM_IMM_TABLE, REF, NULL, 0),
    L(0x28, "I32Load", "i>i", 4),
    L(0x29, "I64Load", "i>l", 8),
    L(0x2a, "F32Load", "i>f", 4),
    L(0x2b, "F64Load", "i>d", 8),
    L(0x2c, "I32Load8S", "i>i", 1),
    L(0x2d, "I32Load8U", "i>i", 1),
    L(0x2e, "I32Load16S", "i>i", 2),
    L(0x2f, "I32Load16U", "i>i", 2),
    L(0x30, "I64Load8S", "i>l", 1),
    L(0x31, "I64Load8U", "i>l", 1),
    L(0x32, "I64Load16S", "i>l", 2),
    L(0x33, "I64Load16U", "i>l", 2),
    L(0x34, "I64Load32S", "i>l", 4),
    L(0x35, "I64Load32U", "i>l", 4),
    L(0x36, "I32Store", "ii>", 4),
    L(0x37, "I64Store", "il>", 8),
    L(0x38, "F32Store", "if>", 4),
    L(0x39, "F64Store", "id>", 8),
    L(0x3a, "I32Store8", "ii>", 1),
    L(0x3b, "I32Store16", "ii>", 2),
    L(0x3c, "I64Store8", "il>", 1),
    L(0x3d, "I64Store16", "il>", 2),
    L(0x3e, "I64Store32", "il>", 4),
    R(0x3f, "MemorySize", WASM_IMM_MEMIDX, 0, ">i", 0),
    R(0x40, "MemoryGrow", WASM_IMM_MEMIDX, 0, "i>i", 0),
    R(0x41, "I32Const", WASM_IMM_I32, 0, ">i", 0),
    R(0x42, "I64Const", WASM_IMM_I64, 0, ">l", 0),
    R(0x43, "F32Const", WASM_IMM_F32, 0, ">f", 0),
    R(0x44, "F64Const", WASM_IMM_F64, 0, ">d", 0),
    N(0x45, "I32Eqz", "i>i"),
    N(0x46, "I32Eq", "ii>i"),
    N(0x47, "I32Ne", "ii>i"),
    N(0x48, "I32LtS", "ii>i"),
    N(0x49, "I32LtU", "ii>i"),
    N(0x4a, "I32GtS", "ii>i"),
    N(0x4b, "I32GtU", "ii>i"),
    N(0x4c, "I32LeS", "ii>i"),
    N(0x4d, "I32LeU", "ii>i"),
    N(0x4e, "I32GeS", "ii>i"),
    N(0x4f, "I32GeU", "ii>i"),
    N(0x50, "I64Eqz", "l>i"),
    N(0x51, "I64Eq", "ll>i"),
    N(0x52, "I64Ne", "ll>i"),
    N(0x53, "I64LtS", "ll>i"),
    N(0x54, "I64LtU", "ll>i"),
    N(0x55, "I64GtS", "ll>i"),
    N(0x56, "I64GtU", "ll>i"),
    N(0x57, "I64LeS", "ll>i"),
    N(0x58, "I64LeU", "ll>i"),
    N(0x59, "I64GeS", "ll>i"),
    N(0x5a, "I64GeU", "ll>i"),
    N(0x5b, "F32Eq", "ff>i"),
    N(0x5c, "F32Ne", "ff>i"),
    N(0x5d, "F32Lt", "ff>i"),
    N(0x5e, "F32Gt", "ff>i"),
    N(0x5f, "F32Le", "ff>i"),
    N(0x60, "F32Ge", "ff>i"),
    N(0x61, "F64Eq", "dd>i"),
    N(0x62, "F64Ne", "dd>i"),
    N(0x63, "F64Lt", "dd>i"),
    N(0x64, "F64Gt", "dd>i"),
    N(0x65, "F64Le", "dd>i"),
    N(0x66, "F64Ge", "dd>i"),
    N(0x67, "I32Clz", "i>i"),
    N(0x68, "I32Ctz", "i>i"),
    N(0x69, "I32Popcnt", "i>i"),
    N(0x6a, "I32Add", "ii>i"),
    N(0x6b, "I32Sub", "ii>i"),
    N(0x6c, "I32Mul", "ii>i"),
    N(0x6d, "I32DivS", "ii>i"),
    N(0x6e, "I32DivU", "ii>i"),
    N(0x6f, "I32RemS", "ii>i"),
    N(0x70, "I32RemU", "ii>i"),
    N(0x71, "I32And", "ii>i"),
    N(0x72, "I32Or", "ii>i"),
    N(0x73, "I32Xor", "ii>i"),
    N(0x74, "I32Shl", "ii>i"),
    N(0x75, "I32ShrS", "ii>i"),
    N(0x76, "I32ShrU", "ii>i"),
    N(0x77, "I32Rotl", "ii>i"),
    N(0x78, "I32Rotr", "ii>i"),
    N(0x79, "I64Clz", "l>l"),
    N(0x7a, "I64Ctz", "l>l"),
    N(0x7b, "I64Popcnt", "l>l"),
    N(0x7c, "I64Add", "ll>l"),
    N(0x7d, "I64Sub", "ll>l"),
    N(0x7e, "I64Mul", "ll>l"),
    N(0x7f, "I64DivS", "ll>l"),
    N(0x80, "I64DivU", "ll>l"),
    N(0x81, "I64RemS", "ll>l"),
    N(0x82, "I64RemU", "ll>l"),
    N(0x83, "I64And", "ll>l"),
    N(0x84, "I64Or", "ll>l"),
    N(0x85, "I64Xor", "ll>l"),
    N(0x86, "I64Shl", "ll>l"),
    N(0x87, "I64ShrS", "ll>l"),
    N(0x88, "I64ShrU", "ll>l"),
    N(0x89, "I64Rotl", "ll>l"),
    N(0x8a, "I64Rotr", "ll>l"),
    N(0x8b, "F32Abs", "f>f"),
    N(0x8c, "F32Neg", "f>f"),
    N(0x8d, "F32Ceil", "f>f"),
    N(0x8e, "F32Floor", "f>f"),
    N(0x8f, "F32Trunc", "f>f"),
    N(0x90, "F32Nearest", "f>f"),
    N(0x91, "F32Sqrt", "f>f"),
    N(0x92, "F32Add", "ff>f"),
    N(0x93, "F32Sub", "ff>f"),
    N(0x94, "F32Mul", "ff>f"),
    N(0x95, "F32Div", "ff>f"),
    N(0x96, "F32Min", "ff>f"),
    N(0x97, "F32Max", "ff>f"),
    N(0x98, "F32Copysign", "ff>f"),
    N(0x99, "F64Abs", "d>d"),
    N(0x9a, "F64Neg", "d>d"),
    N(0x9b, "F64Ceil", "d>d"),
    N(0x9c, "F64Floor", "d>d"),
    N(0x9d, "F64Trunc", "d>d"),
    N(0x9e, "F64Nearest", "d>d"),
    N(0x9f, "F64Sqrt", "d>d"),
    N(0xa0, "F64Add", "dd>d"),
    N(0xa1, "F64Sub", "dd>d"),
    N(0xa2, "F64Mul", "dd>d"),
    N(0xa3, "F64Div", "dd>d"),
    N(0xa4, "F64Min", "dd>d"),
    N(0xa5, "F64Max", "dd>d"),
    N(0xa6, "F64Copysign", "dd>d"),
    N(0xa7, "I32WrapI64", "l>i"),
    N(0xa8, "I32TruncF32S", "f>i"),
    N(0xa9, "I32TruncF32U", "f>i"),
    N(0xaa, "I32TruncF64S", "d>i"),
    N(0xab, "I32TruncF64U", "d>i"),
    N(0xac, "I64ExtendI32S", "i>l"),
    N(0xad, "I64ExtendI32U", "i>l"),
    N(0xae, "I64TruncF32S", "f>l"),
    N(0xaf, "I64TruncF32U", "f>l"),
    N(0xb0, "I64TruncF64S", "d>l"),
    N(0xb1, "I64TruncF64U", "d>l"),
    N(0xb2, "F32ConvertI32S", "i>f"),
    N(0xb3, "F32ConvertI32U", "i>f"),
    N(0xb4, "F32ConvertI64S", "l>f"),
    N(0xb5, "F32ConvertI64U", "l>f"),
    N(0xb6, "F32DemoteF64", "d>f"),
    N(0xb7, "F64ConvertI32S", "i>d"),
    N(0xb8, "F64ConvertI32U", "i>d"),
    N(0xb9, "F64ConvertI64S", "l>d"),
    N(0xba, "F64ConvertI64U", "l>d"),
    N(0xbb, "F64PromoteF32", "f>d"),
    N(0xbc, "I32ReinterpretF32", "f>i"),
    N(0xbd, "I64ReinterpretF64", "d>l"),
    N(0xbe, "F32ReinterpretI32", "i>f"),
    N(0xbf, "F64ReinterpretI64", "l>d"),
    R(0xc0, "I32Extend8S", WASM_IMM_NONE, WASM_F_SIGN_EXT, "i>i", 0),
    R(0xc1, "I32Extend16S", WASM_IMM_NONE, WASM_F_SIGN_EXT, "i>i", 0),
    R(0xc2, "I64Extend8S", WASM_IMM_NONE, WASM_F_SIGN_EXT, "l>l", 0),
    R(0xc3, "I64Extend16S", WASM_IMM_NONE, WASM_F_SIGN_EXT, "l>l", 0),
    R(0xc4, "I64Extend32S", WASM_IMM_NONE, WASM_F_SIGN_EXT, "l>l", 0),
    R(0xd0, "RefNull", WASM_IMM_REFTYPE, REF, NULL, 0),
    R(0xd1, "RefIsNull", WASM_IMM_NONE, REF, NULL, 0),
    R(0xd2, "RefFunc", WASM_IMM_FUNC, REF, NULL, 0),
};

static const wasm_opinfo fc_ops[18] = {
    {WASM_OP_FC(0), "I32TruncSatF32S", WASM_IMM_NONE, SAT, "f>i", 0},
    {WASM_OP_FC(1), "I32TruncSatF32U", WASM_IMM_NONE, SAT, "f>i", 0},
    {WASM_OP_FC(2), "I32TruncSatF64S", WASM_IMM_NONE, SAT, "d>i", 0},
    {WASM_OP_FC(3), "I32TruncSatF64U", WASM_IMM_NONE, SAT, "d>i", 0},
    {WASM_OP_FC(4), "I64TruncSatF32S", WASM_IMM_NONE, SAT, "f>l", 0},
    {WASM_OP_FC(5), "I64TruncSatF32U", WASM_IMM_NONE, SAT, "f>l", 0},
    {WASM_OP_FC(6), "I64TruncSatF64S", WASM_IMM_NONE, SAT, "d>l", 0},
    {WASM_OP_FC(7), "I64TruncSatF64U", WASM_IMM_NONE, SAT, "d>l", 0},
    {WASM_OP_FC(8), "MemoryInit", WASM_IMM_MEMORY_INIT, BULK, NULL, 0},
    {WASM_OP_FC(9), "DataDrop", WASM_IMM_DATA, BULK, NULL, 0},
    {WASM_OP_FC(10), "MemoryCopy", WASM_IMM_MEMORY_COPY, BULK, NULL, 0},
    {WASM_OP_FC(11), "MemoryFill", WASM_IMM_MEMIDX, BULK, NULL, 0},
    {WASM_OP_FC(12), "TableInit", WASM_IMM_TABLE_INIT, BULK, NULL, 0},
    {WASM_OP_FC(13), "ElemDrop", WASM_IMM_ELEM, BULK, NULL, 0},
    {WASM_OP_FC(14), "TableCopy", WASM_IMM_TABLE_COPY, BULK, NULL, 0},
    {WASM_OP_FC(15), "TableGrow", WASM_IMM_TABLE, REF, NULL, 0},
    {WASM_OP_FC(16), "TableSize", WASM_IMM_TABLE, REF, NULL, 0},
    {WASM_OP_FC(17), "TableFill", WASM_IMM_TABLE, REF, NULL, 0},
};

/* SIMD opcodes 0x00-0xff that the final SIMD proposal leaves unassigned. */
static bool simd_unassigned(uint32_t sub) {
    static const uint8_t gaps[] = {0x9a, 0xa2, 0xa5, 0xa6, 0xaf, 0xb0, 0xb2, 0xb3, 0xb4, 0xbb,
                                   0xc2, 0xc5, 0xc6, 0xcf, 0xd0, 0xd2, 0xd3, 0xd4, 0xe2, 0xee};
    for (size_t i = 0; i < sizeof gaps; i++)
        if (gaps[i] == sub) return true;
    return false;
}

static bool simd(uint32_t sub, wasm_opinfo *out) {
    wasm_opinfo o = {WASM_OP_SIMD(sub), "Simd", WASM_IMM_NONE, WASM_F_SIMD, NULL, 0};
    if (sub >= 0x100 && sub <= 0x113) {
        o.name = "RelaxedSimd";
        o.feature = WASM_F_RELAXED_SIMD;
    } else if (sub > 0xff || simd_unassigned(sub)) {
        return false;
    } else if (sub <= 0x0b || sub == 0x5c || sub == 0x5d) {
        o.imm = WASM_IMM_MEMARG;
    } else if (sub == 0x0c) {
        o.imm = WASM_IMM_V128;
    } else if (sub == 0x0d) {
        o.imm = WASM_IMM_SHUFFLE;
    } else if (sub >= 0x15 && sub <= 0x22) {
        o.imm = WASM_IMM_LANE;
    } else if (sub >= 0x54 && sub <= 0x5b) {
        o.imm = WASM_IMM_MEMARG_LANE;
    }
    *out = o;
    return true;
}

static bool atomic(uint32_t sub, wasm_opinfo *out) {
    wasm_opinfo o = {WASM_OP_ATOMIC(sub), "Atomic", WASM_IMM_MEMARG, WASM_F_THREADS, NULL, 0};
    if (sub == 0x03) o.imm = WASM_IMM_ZERO_BYTE;
    else if (sub > 0x02 && (sub < 0x10 || sub > 0x4e)) return false;
    *out = o;
    return true;
}

bool wasm_op_info(uint32_t op, wasm_opinfo *out) {
    uint32_t prefix = op >> 16, sub = op & 0xffff;
    if (prefix == 0) {
        if (op > 0xff || !main_ops[op].name) return false;
        *out = main_ops[op];
        return true;
    }
    if (prefix == WASM_PREFIX_FC) {
        if (sub >= sizeof fc_ops / sizeof fc_ops[0]) return false;
        *out = fc_ops[sub];
        return true;
    }
    if (prefix == WASM_PREFIX_SIMD) return simd(sub, out);
    if (prefix == WASM_PREFIX_ATOMIC) return atomic(sub, out);
    return false;
}
