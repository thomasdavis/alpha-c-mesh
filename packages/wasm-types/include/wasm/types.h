/* @ajax/wasm-types: the WebAssembly binary format's constants (spec 5.3-5.5), and the feature bits
 * the decoder and validator are configured with. Header-only, freestanding. */
#ifndef WASM_TYPES_H
#define WASM_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* Value types, by their binary encoding. */
typedef uint8_t wasm_valtype;
#define WASM_I32 0x7f
#define WASM_I64 0x7e
#define WASM_F32 0x7d
#define WASM_F64 0x7c
#define WASM_V128 0x7b
#define WASM_FUNCREF 0x70
#define WASM_EXTERNREF 0x6f
/* Block type: no result. */
#define WASM_BLOCK_EMPTY 0x40
/* Function type form. */
#define WASM_FUNC_FORM 0x60

static inline bool wasm_is_num(wasm_valtype t) { return t == WASM_I32 || t == WASM_I64 || t == WASM_F32 || t == WASM_F64; }
static inline bool wasm_is_ref(wasm_valtype t) { return t == WASM_FUNCREF || t == WASM_EXTERNREF; }
static inline bool wasm_is_valtype(wasm_valtype t) { return wasm_is_num(t) || t == WASM_V128 || wasm_is_ref(t); }

/* The names wasmparser prints for them. */
static inline const char *wasm_valtype_name(wasm_valtype t) {
    switch (t) {
    case WASM_I32: return "i32";
    case WASM_I64: return "i64";
    case WASM_F32: return "f32";
    case WASM_F64: return "f64";
    case WASM_V128: return "v128";
    case WASM_FUNCREF: return "funcref";
    case WASM_EXTERNREF: return "externref";
    default: return "?";
    }
}

/* Section ids. */
enum {
    WASM_SEC_CUSTOM = 0,
    WASM_SEC_TYPE = 1,
    WASM_SEC_IMPORT = 2,
    WASM_SEC_FUNCTION = 3,
    WASM_SEC_TABLE = 4,
    WASM_SEC_MEMORY = 5,
    WASM_SEC_GLOBAL = 6,
    WASM_SEC_EXPORT = 7,
    WASM_SEC_START = 8,
    WASM_SEC_ELEMENT = 9,
    WASM_SEC_CODE = 10,
    WASM_SEC_DATA = 11,
    WASM_SEC_DATACOUNT = 12,
    WASM_SEC_TAG = 13,
};

/* Import and export kinds. */
enum { WASM_EXTERN_FUNC = 0, WASM_EXTERN_TABLE = 1, WASM_EXTERN_MEMORY = 2, WASM_EXTERN_GLOBAL = 3, WASM_EXTERN_TAG = 4 };

/* Limits flags (memory and table types). */
#define WASM_LIMITS_HAS_MAX 0x01
#define WASM_LIMITS_SHARED 0x02
#define WASM_LIMITS_64 0x04

/* A memory page; the most pages a 32-bit memory may have; the most table elements we allow. */
#define WASM_PAGE 65536u
#define WASM_MAX_PAGES 65536u
#define WASM_MAX_TABLE 10000000u

/* Proposals beyond the MVP, as feature bits. */
enum {
    WASM_F_MUTABLE_GLOBAL = 1u << 0,
    WASM_F_SIGN_EXT = 1u << 1,
    WASM_F_SAT_FLOAT_TO_INT = 1u << 2,
    WASM_F_MULTI_VALUE = 1u << 3,
    WASM_F_REFERENCE_TYPES = 1u << 4,
    WASM_F_BULK_MEMORY = 1u << 5,
    WASM_F_SIMD = 1u << 6,
    WASM_F_RELAXED_SIMD = 1u << 7,
    WASM_F_THREADS = 1u << 8,
    WASM_F_EXTENDED_CONST = 1u << 9,
    WASM_F_MULTI_MEMORY = 1u << 10,
    WASM_F_MEMORY64 = 1u << 11,
};
typedef uint32_t wasm_features;

/* Every proposal this implementation knows (wasmparser's WasmFeatures::all() for what we decode). */
#define WASM_F_ALL                                                                                                    \
    (WASM_F_MUTABLE_GLOBAL | WASM_F_SIGN_EXT | WASM_F_SAT_FLOAT_TO_INT | WASM_F_MULTI_VALUE | WASM_F_REFERENCE_TYPES | \
     WASM_F_BULK_MEMORY | WASM_F_SIMD | WASM_F_RELAXED_SIMD | WASM_F_THREADS | WASM_F_EXTENDED_CONST |             \
     WASM_F_MULTI_MEMORY | WASM_F_MEMORY64)

/* What the interpreter runs: everything above but SIMD, relaxed SIMD, threads, multi-memory and memory64. */
#define WASM_F_INTERP                                                                                                 \
    (WASM_F_MUTABLE_GLOBAL | WASM_F_SIGN_EXT | WASM_F_SAT_FLOAT_TO_INT | WASM_F_MULTI_VALUE | WASM_F_REFERENCE_TYPES | \
     WASM_F_BULK_MEMORY | WASM_F_EXTENDED_CONST)

#endif
