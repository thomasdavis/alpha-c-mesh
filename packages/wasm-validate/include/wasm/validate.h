/* @ajax/wasm-validate: the spec's validation (chapter 3) of a decoded module, for the proposals in
 * `features` (wasm/types.h): the module's indices, limits, constant expressions, exports, start,
 * segments, and a type check of every function body with the spec's algorithm (an operand stack of
 * types and a stack of control frames). An instruction of a proposal that isn't enabled is refused
 * the way wasmparser refuses it ("SIMD support is not enabled").
 *
 * SIMD instructions are decoded but not type-checked: where SIMD is enabled, a function's type
 * check stops at its first SIMD instruction and the rest of the body is only walked (for
 * structure and for instructions of proposals that aren't enabled). The interpreter runs no SIMD,
 * so it never runs such a function. */
#ifndef WASM_VALIDATE_H
#define WASM_VALIDATE_H

#include "wasm/module.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool uses_simd;    /* some body holds a SIMD instruction */
    bool uses_threads; /* a shared memory or an atomic instruction */
} wasm_validate_report;

/* true when m is valid under features; else err holds "<what> (at offset 0x<n>)". report may be NULL. */
bool wasm_validate(const wasm_module *m, wasm_features features, wasm_validate_report *report, char *err,
                   size_t errlen);

/* The proposal names in wasmparser's refusals ("SIMD", "sign extension operations"). */
const char *wasm_feature_name(wasm_features f);

#endif
