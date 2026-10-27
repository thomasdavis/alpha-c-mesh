/* @ajax/wasm-opcodes: the instruction set as data.
 *
 * An instruction's id is its opcode byte, or for a prefixed one (prefix << 16) | sub-opcode:
 * i32.trunc_sat_f32_s is 0xfc0000, v128.load 0xfd0000, memory.atomic.notify 0xfe0000. For each
 * known id wasm_op_info gives the immediates that follow it in the binary, its operand signature
 * when it is a plain one ("ii>i": pops i32 i32, pushes i32; NULL: the validator handles it), the
 * proposal that introduced it, and wasmparser's name for it (its Operator variant, as meshd's float
 * guard prints it: "F32Add"). SIMD and atomics are known by their immediates only (name "Simd",
 * "RelaxedSimd", "Atomic"): enough to walk and refuse them, not to run them. */
#ifndef WASM_OPCODES_H
#define WASM_OPCODES_H

#include "wasm/types.h"

#include <stdint.h>

/* Prefixes. */
#define WASM_PREFIX_FC 0xfc
#define WASM_PREFIX_SIMD 0xfd
#define WASM_PREFIX_ATOMIC 0xfe
#define WASM_OP_FC(sub) (0xfc0000u | (sub))
#define WASM_OP_SIMD(sub) (0xfd0000u | (sub))
#define WASM_OP_ATOMIC(sub) (0xfe0000u | (sub))

/* What follows the opcode. */
typedef enum {
    WASM_IMM_NONE,
    WASM_IMM_BLOCK,         /* block type: 0x40, a value type, or an s33 type index */
    WASM_IMM_LABEL,         /* u32 label depth */
    WASM_IMM_BR_TABLE,      /* vec(u32) then u32 */
    WASM_IMM_FUNC,          /* u32 function index */
    WASM_IMM_CALL_INDIRECT, /* u32 type index, u32 table index */
    WASM_IMM_LOCAL,         /* u32 */
    WASM_IMM_GLOBAL,        /* u32 */
    WASM_IMM_TABLE,         /* u32 table index */
    WASM_IMM_MEMARG,        /* u32 align (bit 6: a memory index follows), u32 offset */
    WASM_IMM_MEMIDX,        /* u32 memory index (memory.size, memory.grow, memory.fill) */
    WASM_IMM_I32,           /* s32 */
    WASM_IMM_I64,           /* s64 */
    WASM_IMM_F32,           /* 4 bytes */
    WASM_IMM_F64,           /* 8 bytes */
    WASM_IMM_SELECT_T,      /* vec(valtype) */
    WASM_IMM_REFTYPE,       /* one reference type byte */
    WASM_IMM_MEMORY_INIT,   /* u32 data index, u32 memory index */
    WASM_IMM_DATA,          /* u32 data index */
    WASM_IMM_MEMORY_COPY,   /* u32 memory index, u32 memory index */
    WASM_IMM_TABLE_INIT,    /* u32 element index, u32 table index */
    WASM_IMM_ELEM,          /* u32 element index */
    WASM_IMM_TABLE_COPY,    /* u32 table index, u32 table index */
    WASM_IMM_V128,          /* 16 bytes */
    WASM_IMM_SHUFFLE,       /* 16 lane bytes */
    WASM_IMM_LANE,          /* 1 lane byte */
    WASM_IMM_MEMARG_LANE,   /* memarg, then 1 lane byte */
    WASM_IMM_ZERO_BYTE,     /* one 0x00 byte (atomic.fence) */
} wasm_imm;

typedef struct {
    uint32_t op;
    const char *name;     /* wasmparser's Operator variant */
    uint8_t imm;          /* wasm_imm */
    uint32_t feature;     /* WASM_F_* bit that introduced it; 0: the MVP */
    const char *sig;      /* "ab>c" over i (i32) l (i64) f (f32) d (f64), or NULL */
    uint8_t mem_bytes;    /* loads and stores: the access size in bytes; else 0 */
} wasm_opinfo;

/* The instruction with this id into *out; false when no proposal we know defines it. */
bool wasm_op_info(uint32_t op, wasm_opinfo *out);

/* The value type of a signature letter (i l f d), or 0. */
static inline wasm_valtype wasm_sig_type(char c) {
    switch (c) {
    case 'i': return WASM_I32;
    case 'l': return WASM_I64;
    case 'f': return WASM_F32;
    case 'd': return WASM_F64;
    default: return 0;
    }
}

/* The opcodes the interpreter and validator name directly. */
enum {
    WASM_OP_UNREACHABLE = 0x00,
    WASM_OP_NOP = 0x01,
    WASM_OP_BLOCK = 0x02,
    WASM_OP_LOOP = 0x03,
    WASM_OP_IF = 0x04,
    WASM_OP_ELSE = 0x05,
    WASM_OP_END = 0x0b,
    WASM_OP_BR = 0x0c,
    WASM_OP_BR_IF = 0x0d,
    WASM_OP_BR_TABLE = 0x0e,
    WASM_OP_RETURN = 0x0f,
    WASM_OP_CALL = 0x10,
    WASM_OP_CALL_INDIRECT = 0x11,
    WASM_OP_DROP = 0x1a,
    WASM_OP_SELECT = 0x1b,
    WASM_OP_SELECT_T = 0x1c,
    WASM_OP_LOCAL_GET = 0x20,
    WASM_OP_LOCAL_SET = 0x21,
    WASM_OP_LOCAL_TEE = 0x22,
    WASM_OP_GLOBAL_GET = 0x23,
    WASM_OP_GLOBAL_SET = 0x24,
    WASM_OP_TABLE_GET = 0x25,
    WASM_OP_TABLE_SET = 0x26,
    WASM_OP_MEMORY_SIZE = 0x3f,
    WASM_OP_MEMORY_GROW = 0x40,
    WASM_OP_I32_CONST = 0x41,
    WASM_OP_I64_CONST = 0x42,
    WASM_OP_F32_CONST = 0x43,
    WASM_OP_F64_CONST = 0x44,
    WASM_OP_REF_NULL = 0xd0,
    WASM_OP_REF_IS_NULL = 0xd1,
    WASM_OP_REF_FUNC = 0xd2,
};

#define WASM_OP_MEMORY_INIT WASM_OP_FC(8)
#define WASM_OP_DATA_DROP WASM_OP_FC(9)
#define WASM_OP_MEMORY_COPY WASM_OP_FC(10)
#define WASM_OP_MEMORY_FILL WASM_OP_FC(11)
#define WASM_OP_TABLE_INIT WASM_OP_FC(12)
#define WASM_OP_ELEM_DROP WASM_OP_FC(13)
#define WASM_OP_TABLE_COPY WASM_OP_FC(14)
#define WASM_OP_TABLE_GROW WASM_OP_FC(15)
#define WASM_OP_TABLE_SIZE WASM_OP_FC(16)
#define WASM_OP_TABLE_FILL WASM_OP_FC(17)

#endif
