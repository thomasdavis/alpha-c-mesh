/* @ajax/wasm-module: a decoded module (what @ajax/wasm-decode produces, @ajax/wasm-validate checks
 * and @ajax/wasm-interp runs). Everything points into the binary it came from, which must outlive
 * the module; only the arrays are owned. Index spaces put imports first, as the spec does. */
#ifndef WASM_MODULE_H
#define WASM_MODULE_H

#include "wasm/types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t nparams, nresults;
    const wasm_valtype *params;  /* into the binary */
    const wasm_valtype *results; /* into the binary */
} wasm_functype;

typedef struct {
    uint64_t min, max; /* max only when flags has WASM_LIMITS_HAS_MAX */
    uint8_t flags;
} wasm_limits;

typedef struct {
    wasm_valtype elem;
    wasm_limits limits;
} wasm_tabletype;

typedef struct {
    wasm_valtype type;
    bool mut;
} wasm_globaltype;

/* A constant expression or a function body: bytes up to and including its final `end`. */
typedef struct {
    const uint8_t *start;
    size_t len;
    size_t offset; /* of start in the binary */
} wasm_expr;

typedef struct {
    const uint8_t *module, *name; /* UTF-8, not NUL-terminated */
    uint32_t module_len, name_len;
    uint8_t kind; /* WASM_EXTERN_* */
    uint32_t func_type;
    wasm_tabletype table;
    wasm_limits mem;
    wasm_globaltype global;
} wasm_import;

typedef struct {
    const uint8_t *name;
    uint32_t name_len;
    uint8_t kind;
    uint32_t index;
} wasm_export;

enum { WASM_SEG_ACTIVE, WASM_SEG_PASSIVE, WASM_SEG_DECLARATIVE };

typedef struct {
    uint8_t mode;
    uint32_t table;
    wasm_expr offset;   /* active only */
    wasm_valtype type;  /* WASM_FUNCREF or WASM_EXTERNREF */
    uint32_t count;
    uint32_t *funcs;    /* function indices (flags 0-3), or NULL */
    wasm_expr *exprs;   /* element expressions (flags 4-7), or NULL */
    size_t offset_in_binary;
} wasm_elem;

typedef struct {
    uint8_t mode; /* active or passive */
    uint32_t mem;
    wasm_expr offset;
    const uint8_t *bytes;
    uint32_t len;
    size_t offset_in_binary;
} wasm_data;

typedef struct {
    uint32_t count;
    wasm_valtype type;
} wasm_localgroup;

typedef struct {
    wasm_localgroup *locals;
    uint32_t nlocal_groups;
    uint64_t nlocals; /* declared locals, parameters not included */
    wasm_expr body;   /* the instructions after the local declarations */
    size_t offset;    /* of the entry (its size field) in the binary */
} wasm_code;

typedef struct wasm_module {
    const uint8_t *bytes;
    size_t len;
    wasm_functype *types;
    uint32_t ntypes;
    wasm_import *imports;
    uint32_t nimports;
    uint32_t *funcs; /* type index of every function, imports first */
    uint32_t nfuncs, nfunc_imports;
    wasm_tabletype *tables;
    uint32_t ntables, ntable_imports;
    wasm_limits *mems;
    uint32_t nmems, nmem_imports;
    wasm_globaltype *globals;
    uint32_t nglobals, nglobal_imports;
    wasm_expr *global_inits; /* defined globals only: global i's at [i - nglobal_imports] */
    wasm_export *exports;
    uint32_t nexports;
    bool has_start;
    uint32_t start;
    wasm_elem *elems;
    uint32_t nelems;
    bool has_datacount;
    uint32_t datacount;
    wasm_code *codes; /* defined functions: function i's at [i - nfunc_imports] */
    uint32_t ncodes;
    wasm_data *datas;
    uint32_t ndatas;
    /* Offsets of the code section's start, for messages. */
    size_t code_section_offset;
} wasm_module;

/* Frees the arrays (not the binary). Safe on a zeroed or partly decoded module. */
void wasm_module_free(wasm_module *m);

/* The type of function f (imports first). */
static inline const wasm_functype *wasm_func_type(const wasm_module *m, uint32_t f) { return &m->types[m->funcs[f]]; }

/* The export named name of kind, or NULL. */
const wasm_export *wasm_module_export(const wasm_module *m, const char *name, uint8_t kind);

#endif
