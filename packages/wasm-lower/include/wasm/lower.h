/* @ajax/wasm-lower: validated function bodies to the code @ajax/wasm-interp runs.
 *
 * A function's code is an array of fixed-size instructions. Structured control flow is gone:
 * every branch names its target index, the operand-stack height (in 64-bit slots, from the
 * frame's base) its label's values move to, and how many values move. Locals are slots
 * 0..nlocals of the frame; the operand stack starts after them. Loops start with an interrupt
 * check (as wasmtime's epoch checks sit at loop headers and function entries). Dead code after
 * br, br_table, return and unreachable is dropped.
 *
 * The input must be valid (@ajax/wasm-validate) and free of SIMD and atomics. */
#ifndef WASM_LOWER_H
#define WASM_LOWER_H

#include "wasm/module.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Internal opcodes: plain wasm instructions keep their one-byte opcode; 0xfc-prefixed ones are
 * 0x100 + sub-opcode; the rest are below. */
enum {
    WASM_INS_FC = 0x100,
    WASM_INS_BR = 0x200,   /* a: target; b: height << 32 | values */
    WASM_INS_BR_IF,        /* the same, taken when the popped i32 isn't 0 */
    WASM_INS_BR_UNLESS,    /* a: target, taken (no stack change) when the popped i32 is 0: `if` */
    WASM_INS_JUMP,         /* a: target, no stack change: the end of an `if`'s then-branch */
    WASM_INS_BR_TABLE,     /* a: first entry in the function's br table; b: entries (the default last) */
    WASM_INS_RETURN,       /* a: results */
    WASM_INS_CALL,         /* a: function index */
    WASM_INS_CALL_INDIRECT, /* a: canonical type index; b: table index */
    WASM_INS_INTERRUPT,    /* loop header: trap if the deadline passed */
    WASM_INS_COUNT
};

typedef struct {
    uint16_t op;
    uint16_t pad;
    uint32_t a;
    uint64_t b;
} wasm_ins;

typedef struct {
    uint32_t target;
    uint32_t height;
    uint32_t values;
} wasm_br_entry;

typedef struct {
    wasm_ins *code;
    uint32_t ncode;
    wasm_br_entry *brs;
    uint32_t nbrs;
    uint32_t nparams, nresults;
    uint32_t nlocals;     /* parameters included */
    uint32_t frame_slots; /* nlocals + the deepest operand stack */
} wasm_fn_code;

/* Lowers defined function `func` of m into *out. false (with err) only when out of memory or when
 * the body uses SIMD or atomics, which the interpreter doesn't run. */
bool wasm_lower(const wasm_module *m, uint32_t func, wasm_fn_code *out, char *err, size_t errlen);
void wasm_fn_code_free(wasm_fn_code *c);

/* For call_indirect: canon[t] is the first type index whose signature equals type t's. */
uint32_t *wasm_canonical_types(const wasm_module *m);

#endif
