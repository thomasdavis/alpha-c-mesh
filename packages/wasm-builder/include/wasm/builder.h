/* @ajax/wasm-builder: WebAssembly binaries assembled from C, for tests and tools.
 *
 * A wasm_buf is a growable byte buffer with the binary format's encoders (LEB128, names, floats).
 * A wasm_builder collects a module's parts and wasm_builder_finish writes them as sections in the
 * spec's order. Bodies are raw instruction bytes the caller writes into a wasm_buf (the final `end`
 * is added). Indices are the caller's to get right: this builds invalid modules on request, which
 * is what tests of a validator need. Allocation failure aborts. */
#ifndef WASM_BUILDER_H
#define WASM_BUILDER_H

#include "wasm/types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t len, cap;
} wasm_buf;

void wasm_buf_free(wasm_buf *b);
void wb_byte(wasm_buf *b, uint8_t v);
void wb_bytes(wasm_buf *b, const void *p, size_t n);
void wb_u32(wasm_buf *b, uint32_t v); /* unsigned LEB128 */
void wb_u64(wasm_buf *b, uint64_t v);
void wb_s32(wasm_buf *b, int32_t v); /* signed LEB128 */
void wb_s64(wasm_buf *b, int64_t v);
void wb_f32(wasm_buf *b, float v);
void wb_f64(wasm_buf *b, double v);
void wb_name(wasm_buf *b, const char *s);
/* A section: id, the payload's size, the payload. */
void wb_section(wasm_buf *b, uint8_t id, const wasm_buf *payload);

/* Instruction shorthands. */
void wb_i32_const(wasm_buf *b, int32_t v);
void wb_i64_const(wasm_buf *b, int64_t v);
void wb_op(wasm_buf *b, uint8_t op);                 /* a plain opcode */
void wb_op_u32(wasm_buf *b, uint8_t op, uint32_t a); /* an opcode with one u32 (local.get 3, call 1, br 0) */
void wb_fc(wasm_buf *b, uint32_t sub);               /* 0xfc sub */
void wb_memarg(wasm_buf *b, uint8_t op, uint32_t align, uint32_t offset);

typedef struct {
    char *module, *name;
    uint8_t kind;
    uint32_t type;           /* functions */
    wasm_valtype vtype;      /* globals; tables: element type */
    bool mut;                /* globals */
    uint32_t min, max;       /* memories, tables */
    bool has_max;
} wasm_builder_import;

typedef struct {
    uint32_t type;
    wasm_valtype *locals; /* one entry per local (run-length encoded on output) */
    uint32_t nlocals;
    wasm_buf body; /* instructions without the final end */
} wasm_builder_func;

typedef struct {
    wasm_valtype type;
    bool mut;
    wasm_buf init; /* instructions without the final end */
} wasm_builder_global;

typedef struct {
    char *name;
    uint8_t kind;
    uint32_t index;
} wasm_builder_export;

typedef struct {
    bool passive;
    uint32_t offset; /* active: i32.const offset in memory 0 / table 0 */
    wasm_buf bytes;  /* data: the bytes */
    uint32_t *funcs; /* elements: function indices */
    uint32_t nfuncs;
} wasm_builder_segment;

typedef struct {
    wasm_buf types; /* the type section payload's entries, counted in ntypes */
    uint32_t ntypes;
    wasm_builder_import *imports;
    uint32_t nimports, nfunc_imports;
    wasm_builder_func *funcs;
    uint32_t nfuncs;
    bool has_memory;
    uint32_t mem_min, mem_max;
    bool mem_has_max;
    bool has_table;
    uint32_t table_min, table_max;
    bool table_has_max;
    wasm_builder_global *globals;
    uint32_t nglobals;
    wasm_builder_export *exports;
    uint32_t nexports;
    bool has_start;
    uint32_t start;
    wasm_builder_segment *elems;
    uint32_t nelems;
    wasm_builder_segment *datas;
    uint32_t ndatas;
    bool datacount;
} wasm_builder;

void wasm_builder_free(wasm_builder *w);
/* A function type ("ii>i" over i l f d, F funcref, E externref, V v128); its index. */
uint32_t wasm_builder_type(wasm_builder *w, const char *sig);
uint32_t wasm_builder_import_func(wasm_builder *w, const char *module, const char *name, uint32_t type);
void wasm_builder_import_memory(wasm_builder *w, const char *module, const char *name, uint32_t min);
/* A defined function of `type` with locals (a string as in sig, e.g. "iif"); returns its body
 * buffer to write instructions into. Its index is nfunc_imports + its position. */
wasm_buf *wasm_builder_func_add(wasm_builder *w, uint32_t type, const char *locals, uint32_t *index);
void wasm_builder_memory(wasm_builder *w, uint32_t min, uint32_t max, bool has_max);
void wasm_builder_table(wasm_builder *w, uint32_t min, uint32_t max, bool has_max);
/* A global; returns its init buffer (write a constant instruction into it). */
wasm_buf *wasm_builder_add_global(wasm_builder *w, wasm_valtype type, bool mut);
void wasm_builder_add_export(wasm_builder *w, const char *name, uint8_t kind, uint32_t index);
void wasm_builder_data(wasm_builder *w, uint32_t offset, const void *bytes, size_t n);
void wasm_builder_data_passive(wasm_builder *w, const void *bytes, size_t n);
void wasm_builder_elem(wasm_builder *w, uint32_t offset, const uint32_t *funcs, uint32_t n);
/* The binary. */
void wasm_builder_finish(const wasm_builder *w, wasm_buf *out);

#endif
