/* Validation: the module's rules, then each function body's type check. */
#include "wasm/validate.h"

#include "wasm/instr.h"
#include "wasm/reader.h"

#include <stdlib.h>
#include <string.h>

const char *wasm_feature_name(wasm_features f) {
    switch (f) {
    case WASM_F_MUTABLE_GLOBAL: return "mutable global";
    case WASM_F_SIGN_EXT: return "sign extension operations";
    case WASM_F_SAT_FLOAT_TO_INT: return "saturating float to int conversions";
    case WASM_F_MULTI_VALUE: return "multi-value";
    case WASM_F_REFERENCE_TYPES: return "reference types";
    case WASM_F_BULK_MEMORY: return "bulk memory";
    case WASM_F_SIMD: return "SIMD";
    case WASM_F_RELAXED_SIMD: return "relaxed SIMD";
    case WASM_F_THREADS: return "threads";
    case WASM_F_EXTENDED_CONST: return "extended constant expressions";
    case WASM_F_MULTI_MEMORY: return "multi-memory";
    case WASM_F_MEMORY64: return "memory64";
    default: return "this proposal's";
    }
}

/* ---- the module's rules ------------------------------------------------------------------- */

typedef struct {
    const wasm_module *m;
    wasm_features f;
    wasm_validate_report *rep;
    wasm_reader r;     /* for messages: offsets are into m->bytes */
    bool *declared;    /* functions a ref.func may name */
} M;

static bool need(M *v, wasm_features feat, size_t at) {
    if (v->f & feat) return true;
    return wasm_rd_fail_at(&v->r, at, "%s support is not enabled", wasm_feature_name(feat));
}

static bool valtype_ok(M *v, wasm_valtype t, size_t at) {
    if (t == WASM_V128) return need(v, WASM_F_SIMD, at);
    if (wasm_is_ref(t)) return need(v, WASM_F_REFERENCE_TYPES, at);
    return true;
}

static size_t off(M *v, const void *p) { return (size_t)((const uint8_t *)p - v->m->bytes); }

static bool types_ok(M *v) {
    for (uint32_t i = 0; i < v->m->ntypes; i++) {
        const wasm_functype *t = &v->m->types[i];
        for (uint32_t k = 0; k < t->nparams; k++)
            if (!valtype_ok(v, t->params[k], off(v, &t->params[k]))) return false;
        for (uint32_t k = 0; k < t->nresults; k++)
            if (!valtype_ok(v, t->results[k], off(v, &t->results[k]))) return false;
        if (t->nresults > 1 && !need(v, WASM_F_MULTI_VALUE, off(v, t->results))) return false;
    }
    return true;
}

static bool mem_ok(M *v, const wasm_limits *l, size_t at) {
    bool is64 = l->flags & WASM_LIMITS_64;
    if (is64 && !need(v, WASM_F_MEMORY64, at)) return false;
    if (l->flags & WASM_LIMITS_SHARED) {
        if (!need(v, WASM_F_THREADS, at)) return false;
        if (v->rep) v->rep->uses_threads = true;
        if (!(l->flags & WASM_LIMITS_HAS_MAX)) return wasm_rd_fail_at(&v->r, at, "shared memory must have maximum size");
    }
    uint64_t pages = is64 ? (1ull << 48) : WASM_MAX_PAGES;
    if (l->min > pages)
        return wasm_rd_fail_at(&v->r, at, "memory size must be at most %llu pages (4GiB)", (unsigned long long)pages);
    if (l->flags & WASM_LIMITS_HAS_MAX) {
        if (l->max > pages)
            return wasm_rd_fail_at(&v->r, at, "memory size must be at most %llu pages (4GiB)", (unsigned long long)pages);
        if (l->max < l->min) return wasm_rd_fail_at(&v->r, at, "size minimum must not be greater than maximum");
    }
    return true;
}

static bool table_ok(M *v, const wasm_tabletype *t, size_t at) {
    if (t->elem == WASM_EXTERNREF && !need(v, WASM_F_REFERENCE_TYPES, at)) return false;
    if (t->limits.min > WASM_MAX_TABLE) return wasm_rd_fail_at(&v->r, at, "minimum table size is out of bounds");
    if ((t->limits.flags & WASM_LIMITS_HAS_MAX) && t->limits.max < t->limits.min)
        return wasm_rd_fail_at(&v->r, at, "size minimum must not be greater than maximum");
    return true;
}

/* A constant expression of type want. Constant: the t.const ops, ref.null, ref.func, global.get
 * of an imported immutable global (or, as wasmparser, any earlier immutable one), and with
 * extended constant expressions i32/i64 add, sub and mul. */
static bool const_ok(M *v, const wasm_expr *e, wasm_valtype want, uint32_t globals_visible) {
    wasm_reader r = v->r;
    r.p = e->start;
    r.end = e->start + e->len;
    wasm_valtype stack[64];
    uint32_t sp = 0;
    wasm_instr in;
    for (;;) {
        if (!wasm_rd_instr(&r, &in)) {
            v->r = r;
            return false;
        }
        if (in.op == WASM_OP_END) break;
        wasm_valtype push = 0;
        uint8_t pops = 0;
        switch (in.op) {
        case WASM_OP_I32_CONST: push = WASM_I32; break;
        case WASM_OP_I64_CONST: push = WASM_I64; break;
        case WASM_OP_F32_CONST: push = WASM_F32; break;
        case WASM_OP_F64_CONST: push = WASM_F64; break;
        case WASM_OP_REF_NULL:
            if (!need(v, WASM_F_REFERENCE_TYPES, in.offset)) return false;
            push = in.reftype;
            break;
        case WASM_OP_REF_FUNC:
            if (!need(v, WASM_F_REFERENCE_TYPES, in.offset)) return false;
            if (in.a >= v->m->nfuncs)
                return wasm_rd_fail_at(&v->r, in.offset, "unknown function %u: function index out of bounds", in.a);
            v->declared[in.a] = true;
            push = WASM_FUNCREF;
            break;
        case WASM_OP_GLOBAL_GET:
            if (in.a >= globals_visible || in.a >= v->m->nglobals)
                return wasm_rd_fail_at(&v->r, in.offset, "unknown global %u: global index out of bounds", in.a);
            if (v->m->globals[in.a].mut)
                return wasm_rd_fail_at(&v->r, in.offset, "constant expression required: global.get of mutable global");
            push = v->m->globals[in.a].type;
            break;
        case 0x6a: case 0x6b: case 0x6c: /* i32 add sub mul */
            if (!need(v, WASM_F_EXTENDED_CONST, in.offset)) return false;
            pops = 2;
            push = WASM_I32;
            break;
        case 0x7c: case 0x7d: case 0x7e: /* i64 add sub mul */
            if (!need(v, WASM_F_EXTENDED_CONST, in.offset)) return false;
            pops = 2;
            push = WASM_I64;
            break;
        default:
            return wasm_rd_fail_at(&v->r, in.offset, "constant expression required: non-constant operator: %s", in.info.name);
        }
        for (uint8_t k = 0; k < pops; k++) {
            if (sp == 0 || stack[sp - 1] != push)
                return wasm_rd_fail_at(&v->r, in.offset, "type mismatch: expected %s", wasm_valtype_name(push));
            sp--;
        }
        if (sp == sizeof stack / sizeof stack[0])
            return wasm_rd_fail_at(&v->r, in.offset, "constant expression is too deep");
        stack[sp++] = push;
    }
    if (sp != 1 || stack[0] != want)
        return wasm_rd_fail_at(&v->r, e->offset, "type mismatch: expected %s", wasm_valtype_name(want));
    return true;
}

static bool module_ok(M *v) {
    const wasm_module *m = v->m;
    if (!types_ok(v)) return false;
    for (uint32_t i = 0; i < m->nimports; i++) {
        const wasm_import *im = &m->imports[i];
        size_t at = off(v, im->name);
        switch (im->kind) {
        case WASM_EXTERN_FUNC:
            if (im->func_type >= m->ntypes) return wasm_rd_fail_at(&v->r, at, "unknown type: type index out of bounds");
            break;
        case WASM_EXTERN_TABLE:
            if (!table_ok(v, &im->table, at)) return false;
            break;
        case WASM_EXTERN_MEMORY:
            if (!mem_ok(v, &im->mem, at)) return false;
            break;
        case WASM_EXTERN_GLOBAL:
            if (!valtype_ok(v, im->global.type, at)) return false;
            if (im->global.mut && !need(v, WASM_F_MUTABLE_GLOBAL, at)) return false;
            break;
        }
    }
    for (uint32_t i = m->nfunc_imports; i < m->nfuncs; i++)
        if (m->funcs[i] >= m->ntypes)
            return wasm_rd_fail_at(&v->r, m->codes ? m->codes[i - m->nfunc_imports].offset : 0,
                                   "unknown type: type index out of bounds");
    if (m->ntables > 1 && !need(v, WASM_F_REFERENCE_TYPES, 0)) return false;
    for (uint32_t i = m->ntable_imports; i < m->ntables; i++)
        if (!table_ok(v, &m->tables[i], 0)) return false;
    if (m->nmems > 1 && !(v->f & WASM_F_MULTI_MEMORY)) return wasm_rd_fail_at(&v->r, 0, "multiple memories");
    for (uint32_t i = m->nmem_imports; i < m->nmems; i++)
        if (!mem_ok(v, &m->mems[i], 0)) return false;
    for (uint32_t i = m->nglobal_imports; i < m->nglobals; i++) {
        const wasm_expr *e = &m->global_inits[i - m->nglobal_imports];
        if (!valtype_ok(v, m->globals[i].type, e->offset)) return false;
        if (!const_ok(v, e, m->globals[i].type, i)) return false;
    }
    for (uint32_t i = 0; i < m->nexports; i++) {
        const wasm_export *e = &m->exports[i];
        size_t at = off(v, e->name);
        for (uint32_t k = 0; k < i; k++)
            if (m->exports[k].name_len == e->name_len && memcmp(m->exports[k].name, e->name, e->name_len) == 0)
                return wasm_rd_fail_at(&v->r, at, "duplicate export name");
        uint32_t n = e->kind == WASM_EXTERN_FUNC ? m->nfuncs
                     : e->kind == WASM_EXTERN_TABLE ? m->ntables
                     : e->kind == WASM_EXTERN_MEMORY ? m->nmems
                                                     : m->nglobals;
        if (e->index >= n) return wasm_rd_fail_at(&v->r, at, "exported %s index out of bounds",
                                                  e->kind == WASM_EXTERN_FUNC ? "function" : e->kind == WASM_EXTERN_TABLE ? "table" : e->kind == WASM_EXTERN_MEMORY ? "memory" : "global");
        if (e->kind == WASM_EXTERN_FUNC) v->declared[e->index] = true;
        if (e->kind == WASM_EXTERN_GLOBAL && m->globals[e->index].mut && !need(v, WASM_F_MUTABLE_GLOBAL, at)) return false;
    }
    if (m->has_start) {
        if (m->start >= m->nfuncs) return wasm_rd_fail_at(&v->r, 0, "unknown function %u: start function index out of bounds", m->start);
        const wasm_functype *t = wasm_func_type(m, m->start);
        if (t->nparams || t->nresults) return wasm_rd_fail_at(&v->r, 0, "invalid start function type");
    }
    for (uint32_t i = 0; i < m->nelems; i++) {
        const wasm_elem *e = &m->elems[i];
        size_t at = e->offset_in_binary;
        bool mvp = e->mode == WASM_SEG_ACTIVE && e->table == 0 && e->funcs && e->type == WASM_FUNCREF;
        if (!mvp && !need(v, WASM_F_BULK_MEMORY | WASM_F_REFERENCE_TYPES, at)) return false;
        if (e->type == WASM_EXTERNREF && !need(v, WASM_F_REFERENCE_TYPES, at)) return false;
        if (e->mode == WASM_SEG_ACTIVE) {
            if (e->table >= m->ntables) return wasm_rd_fail_at(&v->r, at, "unknown table %u: table index out of bounds", e->table);
            if (m->tables[e->table].elem != e->type)
                return wasm_rd_fail_at(&v->r, at, "type mismatch: invalid element type");
            if (!const_ok(v, &e->offset, WASM_I32, m->nglobals)) return false;
        }
        for (uint32_t k = 0; k < e->count; k++) {
            if (e->funcs) {
                if (e->funcs[k] >= m->nfuncs)
                    return wasm_rd_fail_at(&v->r, at, "unknown function %u: function index out of bounds", e->funcs[k]);
                v->declared[e->funcs[k]] = true;
            } else if (!const_ok(v, &e->exprs[k], e->type, m->nglobals)) {
                return false;
            }
        }
    }
    for (uint32_t i = 0; i < m->ndatas; i++) {
        const wasm_data *d = &m->datas[i];
        if (d->mode == WASM_SEG_PASSIVE && !need(v, WASM_F_BULK_MEMORY, d->offset_in_binary)) return false;
        if (d->mode == WASM_SEG_ACTIVE) {
            if (d->mem >= m->nmems) return wasm_rd_fail_at(&v->r, d->offset_in_binary, "unknown memory %u", d->mem);
            wasm_valtype it = (m->mems[d->mem].flags & WASM_LIMITS_64) ? WASM_I64 : WASM_I32;
            if (!const_ok(v, &d->offset, it, m->nglobals)) return false;
        }
    }
    return true;
}

/* ---- function bodies ---------------------------------------------------------------------- */

#define UNKNOWN 0 /* a polymorphic stack's value: matches anything */

typedef struct {
    uint32_t op;       /* block, loop, if, else, or 0 for the function */
    int64_t bt;        /* block type (or for the function: -1 and its type in F.ft) */
    wasm_valtype one;  /* a value-type block type's type */
    uint32_t height;   /* operand stack height at entry, params excluded */
    bool unreachable;
} frame;

typedef struct {
    M *v;
    wasm_reader *r; /* the module's: one failure for everything */
    const wasm_functype *ft;
    wasm_valtype *locals;
    uint32_t nlocals;
    wasm_valtype *stack;
    uint32_t sp, scap;
    frame *ctl;
    uint32_t nctl, ccap;
} F;

static bool fail(F *x, size_t at, const char *fmt, const char *a, const char *b) {
    return wasm_rd_fail_at(x->r, at, fmt, a, b);
}

static void block_types(F *x, const frame *fr, bool params, const wasm_valtype **ts, uint32_t *n) {
    if (fr->op == 0) {
        *ts = params ? NULL : x->ft->results;
        *n = params ? 0 : x->ft->nresults;
    } else if (fr->bt >= 0) {
        const wasm_functype *t = &x->v->m->types[fr->bt];
        *ts = params ? t->params : t->results;
        *n = params ? t->nparams : t->nresults;
    } else if (fr->bt == WASM_BT_EMPTY || params) {
        *ts = NULL;
        *n = 0;
    } else {
        *ts = &fr->one;
        *n = 1;
    }
}

/* The types a branch to fr carries: a loop's params, else its results. */
static void label_types(F *x, const frame *fr, const wasm_valtype **ts, uint32_t *n) {
    block_types(x, fr, fr->op == WASM_OP_LOOP, ts, n);
}

static bool push(F *x, wasm_valtype t) {
    if (x->sp == x->scap) {
        uint32_t cap = x->scap ? x->scap * 2 : 64;
        wasm_valtype *s = realloc(x->stack, cap);
        if (!s) return wasm_rd_fail(x->r, "out of memory");
        x->stack = s;
        x->scap = cap;
    }
    x->stack[x->sp++] = t;
    return true;
}

/* Pops one operand; *t is UNKNOWN below an unreachable frame's height. */
static bool pop(F *x, size_t at, wasm_valtype *t) {
    frame *fr = &x->ctl[x->nctl - 1];
    if (x->sp == fr->height) {
        if (fr->unreachable) {
            *t = UNKNOWN;
            return true;
        }
        return wasm_rd_fail_at(x->r, at, "type mismatch: operand stack empty");
    }
    *t = x->stack[--x->sp];
    return true;
}

static bool pop_expect(F *x, size_t at, wasm_valtype want) {
    wasm_valtype t;
    if (!pop(x, at, &t)) return false;
    if (t != UNKNOWN && want != UNKNOWN && t != want)
        return fail(x, at, "type mismatch: expected %s, found %s", wasm_valtype_name(want), wasm_valtype_name(t));
    return true;
}

static bool pop_types(F *x, size_t at, const wasm_valtype *ts, uint32_t n) {
    for (uint32_t i = n; i-- > 0;)
        if (!pop_expect(x, at, ts[i])) return false;
    return true;
}

static bool push_types(F *x, const wasm_valtype *ts, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        if (!push(x, ts[i])) return false;
    return true;
}

static bool push_ctrl(F *x, uint32_t op, int64_t bt, wasm_valtype one) {
    if (x->nctl == x->ccap) {
        uint32_t cap = x->ccap ? x->ccap * 2 : 16;
        frame *c = realloc(x->ctl, cap * sizeof *c);
        if (!c) return wasm_rd_fail(x->r, "out of memory");
        x->ctl = c;
        x->ccap = cap;
    }
    frame *fr = &x->ctl[x->nctl++];
    fr->op = op;
    fr->bt = bt;
    fr->one = one;
    fr->height = x->sp;
    fr->unreachable = false;
    const wasm_valtype *ts;
    uint32_t n;
    block_types(x, fr, true, &ts, &n);
    return push_types(x, ts, n);
}

/* Pops the innermost frame, checking its results are exactly what's on the stack. */
static bool pop_ctrl(F *x, size_t at, frame *out) {
    frame *fr = &x->ctl[x->nctl - 1];
    const wasm_valtype *ts;
    uint32_t n;
    block_types(x, fr, false, &ts, &n);
    if (!pop_types(x, at, ts, n)) return false;
    if (x->sp != fr->height) return wasm_rd_fail_at(x->r, at, "type mismatch: values remaining on stack at end of block");
    *out = *fr;
    x->nctl--;
    return true;
}

static void unreachable(F *x) {
    frame *fr = &x->ctl[x->nctl - 1];
    x->sp = fr->height;
    fr->unreachable = true;
}

static bool blocktype_ok(F *x, const wasm_instr *in) {
    if (in->blocktype >= 0) {
        if ((uint64_t)in->blocktype >= x->v->m->ntypes)
            return wasm_rd_fail_at(x->r, in->offset, "unknown type: type index out of bounds");
        const wasm_functype *t = &x->v->m->types[in->blocktype];
        if ((t->nparams || t->nresults > 1) && !need(x->v, WASM_F_MULTI_VALUE, in->offset)) return false;
        return true;
    }
    if (in->blocktype != WASM_BT_EMPTY) return valtype_ok(x->v, in->bt_val, in->offset);
    return true;
}

static const frame *label(F *x, const wasm_instr *in, uint32_t depth) {
    if (depth >= x->nctl) {
        wasm_rd_fail_at(x->r, in->offset, "unknown label: branch depth too large");
        return NULL;
    }
    return &x->ctl[x->nctl - 1 - depth];
}

static bool memarg_ok(F *x, const wasm_instr *in, uint32_t natural) {
    const wasm_module *m = x->v->m;
    if (in->mem != 0 && !need(x->v, WASM_F_MULTI_MEMORY, in->offset)) return false;
    if (in->mem >= m->nmems) return wasm_rd_fail_at(x->r, in->offset, "unknown memory %u", in->mem);
    if (in->align > 63 || (1ull << in->align) > natural)
        return wasm_rd_fail_at(x->r, in->offset, "alignment must not be larger than natural");
    if (!(m->mems[in->mem].flags & WASM_LIMITS_64) && in->moffset > UINT32_MAX)
        return wasm_rd_fail_at(x->r, in->offset, "offset out of range: must be <= 2**32");
    return true;
}

static wasm_valtype addr_type(F *x, uint32_t mem) {
    return mem < x->v->m->nmems && (x->v->m->mems[mem].flags & WASM_LIMITS_64) ? WASM_I64 : WASM_I32;
}

static bool table_index(F *x, const wasm_instr *in, uint32_t t) {
    if (t >= x->v->m->ntables) return wasm_rd_fail_at(x->r, in->offset, "unknown table %u: table index out of bounds", t);
    return true;
}

static bool plain(F *x, const wasm_instr *in) {
    const char *sig = in->info.sig;
    const char *gt = strchr(sig, '>');
    for (const char *c = gt; c-- > sig;)
        if (!pop_expect(x, in->offset, wasm_sig_type(*c))) return false;
    for (const char *c = gt + 1; *c; c++)
        if (!push(x, wasm_sig_type(*c))) return false;
    return true;
}

/* One instruction of a type-checked body. *done: the function's own frame ended. */
static bool instr(F *x, const wasm_instr *in, bool *done) {
    M *v = x->v;
    const wasm_module *m = v->m;
    size_t at = in->offset;
    wasm_valtype t, u;
    if (in->info.sig && in->info.imm != WASM_IMM_MEMARG && in->op != WASM_OP_MEMORY_SIZE && in->op != WASM_OP_MEMORY_GROW)
        return plain(x, in);
    switch (in->op) {
    case WASM_OP_UNREACHABLE: unreachable(x); return true;
    case WASM_OP_NOP: return true;
    case WASM_OP_BLOCK:
    case WASM_OP_LOOP:
    case WASM_OP_IF: {
        if (!blocktype_ok(x, in)) return false;
        if (in->op == WASM_OP_IF && !pop_expect(x, at, WASM_I32)) return false;
        frame tmp = {in->op, in->blocktype, in->bt_val, 0, false};
        const wasm_valtype *ts;
        uint32_t n;
        block_types(x, &tmp, true, &ts, &n);
        if (!pop_types(x, at, ts, n)) return false;
        return push_ctrl(x, in->op, in->blocktype, in->bt_val);
    }
    case WASM_OP_ELSE: {
        frame fr;
        if (x->ctl[x->nctl - 1].op != WASM_OP_IF) return wasm_rd_fail_at(x->r, at, "else found outside of an `if` block");
        if (!pop_ctrl(x, at, &fr)) return false;
        return push_ctrl(x, WASM_OP_ELSE, fr.bt, fr.one);
    }
    case WASM_OP_END: {
        frame fr;
        if (!pop_ctrl(x, at, &fr)) return false;
        /* An if without else must have matching params and results (it falls through empty). */
        if (fr.op == WASM_OP_IF) {
            const wasm_valtype *pin, *pout;
            uint32_t nin, nout;
            block_types(x, &fr, true, &pin, &nin);
            block_types(x, &fr, false, &pout, &nout);
            if (nin != nout || (nin && memcmp(pin, pout, nin) != 0))
                return wasm_rd_fail_at(x->r, at, "type mismatch: else branch missing");
        }
        if (fr.op == 0) {
            *done = true;
            return true;
        }
        const wasm_valtype *ts;
        uint32_t n;
        block_types(x, &fr, false, &ts, &n);
        return push_types(x, ts, n);
    }
    case WASM_OP_BR:
    case WASM_OP_BR_IF: {
        const frame *fr = label(x, in, in->a);
        if (!fr) return false;
        const wasm_valtype *ts;
        uint32_t n;
        label_types(x, fr, &ts, &n);
        if (in->op == WASM_OP_BR_IF && !pop_expect(x, at, WASM_I32)) return false;
        if (!pop_types(x, at, ts, n)) return false;
        if (in->op == WASM_OP_BR) unreachable(x);
        else if (!push_types(x, ts, n)) return false;
        return true;
    }
    case WASM_OP_BR_TABLE: {
        if (!pop_expect(x, at, WASM_I32)) return false;
        uint32_t *targets = malloc(((size_t)in->ntargets + 1) * sizeof *targets);
        if (!targets) return wasm_rd_fail(x->r, "out of memory");
        wasm_instr_targets(in, targets);
        const frame *def = label(x, in, targets[in->ntargets]);
        bool ok = def != NULL;
        const wasm_valtype *dts = NULL;
        uint32_t dn = 0;
        if (ok) label_types(x, def, &dts, &dn);
        for (uint32_t i = 0; ok && i < in->ntargets; i++) {
            const frame *fr = label(x, in, targets[i]);
            if (!fr) {
                ok = false;
                break;
            }
            const wasm_valtype *ts;
            uint32_t n;
            label_types(x, fr, &ts, &n);
            if (n != dn) {
                ok = wasm_rd_fail_at(x->r, at, "type mismatch: br_table target labels have different number of types");
                break;
            }
            /* Each target's types must match the operands (checked without consuming them). */
            uint32_t sp = x->sp;
            ok = pop_types(x, at, ts, n);
            x->sp = sp;
        }
        free(targets);
        if (!ok) return false;
        if (!pop_types(x, at, dts, dn)) return false;
        unreachable(x);
        return true;
    }
    case WASM_OP_RETURN:
        if (!pop_types(x, at, x->ft->results, x->ft->nresults)) return false;
        unreachable(x);
        return true;
    case WASM_OP_CALL: {
        if (in->a >= m->nfuncs) return wasm_rd_fail_at(x->r, at, "unknown function %u: function index out of bounds", in->a);
        const wasm_functype *ft = wasm_func_type(m, in->a);
        return pop_types(x, at, ft->params, ft->nparams) && push_types(x, ft->results, ft->nresults);
    }
    case WASM_OP_CALL_INDIRECT: {
        if (in->b != 0 && !need(v, WASM_F_REFERENCE_TYPES, at)) return false;
        if (!table_index(x, in, in->b)) return false;
        if (m->tables[in->b].elem != WASM_FUNCREF)
            return wasm_rd_fail_at(x->r, at, "indirect calls must go through a table with type <= funcref");
        if (in->a >= m->ntypes) return wasm_rd_fail_at(x->r, at, "unknown type: type index out of bounds");
        const wasm_functype *ft = &m->types[in->a];
        return pop_expect(x, at, WASM_I32) && pop_types(x, at, ft->params, ft->nparams) &&
               push_types(x, ft->results, ft->nresults);
    }
    case WASM_OP_DROP: return pop(x, at, &t);
    case WASM_OP_SELECT: {
        if (!pop_expect(x, at, WASM_I32) || !pop(x, at, &t) || !pop(x, at, &u)) return false;
        if (t != UNKNOWN && u != UNKNOWN && t != u)
            return fail(x, at, "type mismatch: select operands have different types (%s, %s)", wasm_valtype_name(u), wasm_valtype_name(t));
        wasm_valtype r = t != UNKNOWN ? t : u;
        if (r != UNKNOWN && !wasm_is_num(r) && r != WASM_V128)
            return wasm_rd_fail_at(x->r, at, "type mismatch: select only takes integral types");
        return push(x, r);
    }
    case WASM_OP_SELECT_T:
        if (!valtype_ok(v, in->select_type, at)) return false;
        return pop_expect(x, at, WASM_I32) && pop_expect(x, at, in->select_type) &&
               pop_expect(x, at, in->select_type) && push(x, in->select_type);
    case WASM_OP_LOCAL_GET:
    case WASM_OP_LOCAL_SET:
    case WASM_OP_LOCAL_TEE: {
        if (in->a >= x->nlocals) return wasm_rd_fail_at(x->r, at, "unknown local %u: local index out of bounds", in->a);
        wasm_valtype lt = x->locals[in->a];
        if (in->op == WASM_OP_LOCAL_GET) return push(x, lt);
        if (!pop_expect(x, at, lt)) return false;
        return in->op == WASM_OP_LOCAL_TEE ? push(x, lt) : true;
    }
    case WASM_OP_GLOBAL_GET:
    case WASM_OP_GLOBAL_SET:
        if (in->a >= m->nglobals) return wasm_rd_fail_at(x->r, at, "unknown global %u: global index out of bounds", in->a);
        if (in->op == WASM_OP_GLOBAL_GET) return push(x, m->globals[in->a].type);
        if (!m->globals[in->a].mut) return wasm_rd_fail_at(x->r, at, "global is immutable: cannot modify it with `global.set`");
        return pop_expect(x, at, m->globals[in->a].type);
    case WASM_OP_TABLE_GET:
        if (!table_index(x, in, in->a)) return false;
        return pop_expect(x, at, WASM_I32) && push(x, m->tables[in->a].elem);
    case WASM_OP_TABLE_SET:
        if (!table_index(x, in, in->a)) return false;
        return pop_expect(x, at, m->tables[in->a].elem) && pop_expect(x, at, WASM_I32);
    case WASM_OP_MEMORY_SIZE:
    case WASM_OP_MEMORY_GROW: {
        if (in->mem != 0 && !need(v, WASM_F_MULTI_MEMORY, at)) return false;
        if (in->mem >= m->nmems) return wasm_rd_fail_at(x->r, at, "unknown memory %u", in->mem);
        wasm_valtype it = addr_type(x, in->mem);
        if (in->op == WASM_OP_MEMORY_GROW && !pop_expect(x, at, it)) return false;
        return push(x, it);
    }
    case WASM_OP_REF_NULL: return push(x, in->reftype);
    case WASM_OP_REF_IS_NULL:
        if (!pop(x, at, &t)) return false;
        if (t != UNKNOWN && !wasm_is_ref(t))
            return fail(x, at, "type mismatch: expected %s, found %s", "reference", wasm_valtype_name(t));
        return push(x, WASM_I32);
    case WASM_OP_REF_FUNC:
        if (in->a >= m->nfuncs) return wasm_rd_fail_at(x->r, at, "unknown function %u: function index out of bounds", in->a);
        if (!v->declared[in->a]) return wasm_rd_fail_at(x->r, at, "undeclared function reference");
        return push(x, WASM_FUNCREF);
    case WASM_OP_MEMORY_INIT:
        if (!m->has_datacount) return wasm_rd_fail_at(x->r, at, "data count section required");
        if (in->mem >= m->nmems) return wasm_rd_fail_at(x->r, at, "unknown memory %u", in->mem);
        if (in->a >= m->datacount) return wasm_rd_fail_at(x->r, at, "unknown data segment %u", in->a);
        return pop_expect(x, at, WASM_I32) && pop_expect(x, at, WASM_I32) && pop_expect(x, at, addr_type(x, in->mem));
    case WASM_OP_DATA_DROP:
        if (!m->has_datacount) return wasm_rd_fail_at(x->r, at, "data count section required");
        if (in->a >= m->datacount) return wasm_rd_fail_at(x->r, at, "unknown data segment %u", in->a);
        return true;
    case WASM_OP_MEMORY_COPY:
        if ((in->mem || in->b) && !need(v, WASM_F_MULTI_MEMORY, at)) return false;
        if (in->mem >= m->nmems || in->b >= m->nmems) return wasm_rd_fail_at(x->r, at, "unknown memory %u", in->mem >= m->nmems ? in->mem : in->b);
        return pop_expect(x, at, addr_type(x, in->mem)) && pop_expect(x, at, addr_type(x, in->b)) &&
               pop_expect(x, at, addr_type(x, in->mem));
    case WASM_OP_MEMORY_FILL:
        if (in->mem != 0 && !need(v, WASM_F_MULTI_MEMORY, at)) return false;
        if (in->mem >= m->nmems) return wasm_rd_fail_at(x->r, at, "unknown memory %u", in->mem);
        return pop_expect(x, at, addr_type(x, in->mem)) && pop_expect(x, at, WASM_I32) && pop_expect(x, at, addr_type(x, in->mem));
    case WASM_OP_TABLE_INIT:
        if (!table_index(x, in, in->b)) return false;
        if (in->a >= m->nelems) return wasm_rd_fail_at(x->r, at, "unknown elem segment %u: segment index out of bounds", in->a);
        if (m->elems[in->a].type != m->tables[in->b].elem) return wasm_rd_fail_at(x->r, at, "type mismatch");
        return pop_expect(x, at, WASM_I32) && pop_expect(x, at, WASM_I32) && pop_expect(x, at, WASM_I32);
    case WASM_OP_ELEM_DROP:
        if (in->a >= m->nelems) return wasm_rd_fail_at(x->r, at, "unknown elem segment %u: segment index out of bounds", in->a);
        return true;
    case WASM_OP_TABLE_COPY:
        if (!table_index(x, in, in->a) || !table_index(x, in, in->b)) return false;
        if (m->tables[in->a].elem != m->tables[in->b].elem) return wasm_rd_fail_at(x->r, at, "type mismatch");
        return pop_expect(x, at, WASM_I32) && pop_expect(x, at, WASM_I32) && pop_expect(x, at, WASM_I32);
    case WASM_OP_TABLE_GROW:
        if (!table_index(x, in, in->a)) return false;
        return pop_expect(x, at, WASM_I32) && pop_expect(x, at, m->tables[in->a].elem) && push(x, WASM_I32);
    case WASM_OP_TABLE_SIZE:
        if (!table_index(x, in, in->a)) return false;
        return push(x, WASM_I32);
    case WASM_OP_TABLE_FILL:
        if (!table_index(x, in, in->a)) return false;
        return pop_expect(x, at, WASM_I32) && pop_expect(x, at, m->tables[in->a].elem) && pop_expect(x, at, WASM_I32);
    default: break;
    }
    if (in->info.imm == WASM_IMM_MEMARG) {
        if (!memarg_ok(x, in, in->info.mem_bytes)) return false;
        /* A load or store: the address is the memory's index type, then the signature. */
        const char *sig = in->info.sig;
        const char *gt = strchr(sig, '>');
        for (const char *c = gt; c-- > sig + 1;)
            if (!pop_expect(x, at, wasm_sig_type(*c))) return false;
        if (!pop_expect(x, at, addr_type(x, in->mem))) return false;
        for (const char *c = gt + 1; *c; c++)
            if (!push(x, wasm_sig_type(*c))) return false;
        return true;
    }
    return wasm_rd_fail_at(x->r, at, "internal: unhandled instruction %s", in->info.name);
}

/* The rest of a body past its first SIMD instruction: structure and features only. */
static bool walk_rest(F *x, uint32_t depth) {
    wasm_instr in;
    while (depth > 0) {
        if (wasm_rd_eof(x->r)) return wasm_rd_fail(x->r, "control frames remain at end of function: END opcode expected");
        if (!wasm_rd_instr(x->r, &in)) return false;
        if (in.info.feature && !need(x->v, in.info.feature, in.offset)) return false;
        if (in.info.feature == WASM_F_THREADS && x->v->rep) x->v->rep->uses_threads = true;
        if (in.op == WASM_OP_BLOCK || in.op == WASM_OP_LOOP || in.op == WASM_OP_IF) depth++;
        else if (in.op == WASM_OP_END) depth--;
    }
    return true;
}

static bool body_ok(M *v, uint32_t index) {
    const wasm_module *m = v->m;
    const wasm_code *c = &m->codes[index - m->nfunc_imports];
    F x = {0};
    x.v = v;
    x.r = &v->r;
    v->r.p = c->body.start;
    v->r.end = c->body.start + c->body.len;
    x.ft = wasm_func_type(m, index);
    x.nlocals = (uint32_t)(x.ft->nparams + c->nlocals);
    bool ok = false;
    if (!(x.locals = malloc(x.nlocals ? x.nlocals : 1))) {
        wasm_rd_fail(x.r, "out of memory");
        goto out;
    }
    if (x.ft->nparams) memcpy(x.locals, x.ft->params, x.ft->nparams);
    uint32_t k = x.ft->nparams;
    for (uint32_t g = 0; g < c->nlocal_groups; g++) {
        if (!valtype_ok(v, c->locals[g].type, c->offset)) goto out;
        memset(x.locals + k, c->locals[g].type, c->locals[g].count);
        k += c->locals[g].count;
    }
    if (!push_ctrl(&x, 0, -1, 0)) goto out;
    wasm_instr in;
    bool done = false;
    while (!done) {
        if (wasm_rd_eof(x.r)) {
            wasm_rd_fail(x.r, "control frames remain at end of function: END opcode expected");
            goto out;
        }
        if (!wasm_rd_instr(x.r, &in)) goto out;
        if (in.info.feature && !need(v, in.info.feature, in.offset)) goto out;
        if (in.info.feature & (WASM_F_SIMD | WASM_F_RELAXED_SIMD | WASM_F_THREADS)) {
            /* Enabled, but not type-checked here (see validate.h): walk the rest. */
            if (v->rep) {
                if (in.info.feature == WASM_F_THREADS) v->rep->uses_threads = true;
                else v->rep->uses_simd = true;
            }
            if (!walk_rest(&x, x.nctl)) goto out;
            break;
        }
        if (!instr(&x, &in, &done)) goto out;
    }
    if (!wasm_rd_eof(x.r)) {
        wasm_rd_fail(x.r, "operators remaining after end of function");
        goto out;
    }
    ok = true;
out:
    free(x.locals);
    free(x.stack);
    free(x.ctl);
    return ok;
}

bool wasm_validate(const wasm_module *m, wasm_features features, wasm_validate_report *report, char *err,
                   size_t errlen) {
    M v = {0};
    v.m = m;
    v.f = features;
    v.rep = report;
    if (report) memset(report, 0, sizeof *report);
    v.r = wasm_reader_at(m->bytes, m->bytes, m->bytes + m->len, err, errlen);
    v.declared = calloc(m->nfuncs ? m->nfuncs : 1, sizeof *v.declared);
    if (!v.declared) return wasm_rd_fail(&v.r, "out of memory");
    bool ok = module_ok(&v);
    for (uint32_t i = m->nfunc_imports; ok && i < m->nfuncs; i++) ok = body_ok(&v, i);
    free(v.declared);
    return ok && !v.r.failed;
}
