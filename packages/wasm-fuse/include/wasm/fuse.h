/* @ajax/wasm-fuse: superinstructions for @ajax/wasm-interp. Over a function's lowered code
 * (@ajax/wasm-lower), an operand-making instruction directly before a plain one -- `local.get A`
 * or a constant before an arithmetic, compare, conversion, load or store -- becomes that
 * instruction's operand, and `local.get A; local.get B` one instruction:
 *
 *   WASM_INS_L + op   push fp[a], then op           (a: the local)
 *   WASM_INS_I + op   push the constant, then op    (b, or a for loads and stores: their b is the offset)
 *   WASM_INS_GET2     push fp[a], fp[b]
 *
 * Nothing is fused across a branch target (a jump must land on a whole instruction), and every
 * target -- branches, br_table entries -- is remapped. The interpreter runs a fused instruction as
 * its operand push followed by the plain instruction's code, without a second dispatch. */
#ifndef WASM_FUSE_H
#define WASM_FUSE_H

#include "wasm/lower.h"

#include <stdbool.h>

enum {
    WASM_INS_L = 0x400,      /* + a plain op (< 0x200) */
    WASM_INS_I = 0x600,      /* + a plain op (< 0x200) */
    WASM_INS_GET2 = 0x800,
    WASM_INS_FUSED_END
};

/* Fuses c in place. False only when out of memory (c is then unchanged). */
bool wasm_fuse(wasm_fn_code *c);

/* Whether a lowered instruction is a plain one fusion may take an operand into: one of the
 * opcodes with a signature (not a constant), a load or a store. */
bool wasm_fusible(uint16_t op);

#endif
