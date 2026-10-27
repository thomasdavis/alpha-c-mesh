/* @ajax/wasm-decode: a WebAssembly binary into a wasm_module.
 *
 * Checks what the binary format requires: the header, section order and sizes, every vector and
 * LEB, names' UTF-8, limits flags, segment kinds, the function/code and data-count agreements, and
 * that constant expressions are well-formed instruction sequences. Function bodies are kept as
 * byte ranges for @ajax/wasm-validate. Hostile input never reads out of bounds, and counts are
 * checked against the bytes left before anything is allocated. */
#ifndef WASM_DECODE_H
#define WASM_DECODE_H

#include "wasm/module.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Locals a function may declare (wasmparser's MAX_WASM_FUNCTION_LOCALS). */
#define WASM_MAX_LOCALS 50000u

/* Decodes bytes[0..len) into *out (which then borrows bytes). false: *out is freed and err holds
 * "<what> (at offset 0x<n>)". */
bool wasm_decode(const uint8_t *bytes, size_t len, wasm_module *out, char *err, size_t errlen);

#endif
