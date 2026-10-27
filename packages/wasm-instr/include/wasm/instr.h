/* @ajax/wasm-instr: one instruction at a time from a body or a constant expression.
 *
 * wasm_rd_instr reads the opcode (and the sub-opcode of a prefixed one), looks it up in
 * @ajax/wasm-opcodes and reads its immediates into a wasm_instr. It checks encodings only (an
 * unknown opcode, a malformed LEB, a bad block type byte); typing is @ajax/wasm-validate's. */
#ifndef WASM_INSTR_H
#define WASM_INSTR_H

#include "wasm/opcodes.h"
#include "wasm/reader.h"

#include <stdbool.h>
#include <stdint.h>

/* Block types: WASM_BT_EMPTY, a value type (the byte), or a type index (>= 0). */
#define WASM_BT_EMPTY (-64)

typedef struct {
    uint32_t op;
    wasm_opinfo info;
    size_t offset;          /* of the opcode */
    uint32_t a, b;          /* indices: label, function, local, global, table, type, data, element */
    uint32_t align, mem;    /* memarg: log2 alignment, memory index */
    uint64_t moffset;       /* memarg offset */
    int64_t i;              /* i32.const (sign-extended) and i64.const */
    uint32_t f32;           /* f32.const bits */
    uint64_t f64;           /* f64.const bits */
    int64_t blocktype;      /* WASM_BT_EMPTY, -(0x80 - valtype) for a value type, or a type index */
    wasm_valtype bt_val;    /* the value type when blocktype is one */
    uint32_t ntargets;      /* br_table: targets before the default */
    const uint8_t *targets; /* br_table: the encoded targets (wasm_instr_target reads them) */
    uint32_t nselect;       /* select t*: how many types (1 in practice) */
    wasm_valtype select_type;
    wasm_valtype reftype;   /* ref.null */
    uint8_t lane;           /* SIMD lane immediate */
    const uint8_t *v128;    /* SIMD 16-byte immediate */
} wasm_instr;

/* Reads one instruction. false (and the reader's failure set) on a malformed or unknown one. */
bool wasm_rd_instr(wasm_reader *r, wasm_instr *out);

/* br_table's targets: out must hold ntargets + 1 (the default last). */
void wasm_instr_targets(const wasm_instr *in, uint32_t *out);

/* Whether a block type is a value type (and which), empty, or a type index. */
static inline bool wasm_bt_is_index(const wasm_instr *in) { return in->blocktype >= 0; }

#endif
