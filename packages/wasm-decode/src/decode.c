/* The sections of a module, in order. */
#include "wasm/decode.h"

#include "wasm/instr.h"
#include "wasm/reader.h"

#include <stdlib.h>
#include <string.h>

/* calloc of n elements, at least one so a zero count still gives a non-NULL array. */
static void *alloc(size_t n, size_t size) { return calloc(n ? n : 1, size); }

static bool oom(wasm_reader *r) { return wasm_rd_fail(r, "out of memory"); }

static wasm_valtype valtype(wasm_reader *r) {
    size_t at = wasm_rd_offset(r);
    uint8_t t = wasm_rd_u8(r);
    if (!r->failed && !wasm_is_valtype(t)) wasm_rd_fail_at(r, at, "invalid value type");
    return t;
}

static void limits(wasm_reader *r, wasm_limits *l, bool table) {
    size_t at = wasm_rd_offset(r);
    uint8_t f = wasm_rd_u8(r);
    if (r->failed) return;
    if (f > 0x07 || (table && f > 0x01)) {
        wasm_rd_fail_at(r, at, table ? "invalid table resizable limits flags" : "invalid memory limits flags");
        return;
    }
    l->flags = f;
    bool is64 = f & WASM_LIMITS_64;
    l->min = is64 ? wasm_rd_u64(r) : wasm_rd_u32(r);
    if (f & WASM_LIMITS_HAS_MAX) l->max = is64 ? wasm_rd_u64(r) : wasm_rd_u32(r);
}

static void tabletype(wasm_reader *r, wasm_tabletype *t) {
    size_t at = wasm_rd_offset(r);
    t->elem = wasm_rd_u8(r);
    if (!r->failed && !wasm_is_ref(t->elem)) {
        wasm_rd_fail_at(r, at, "malformed reference type");
        return;
    }
    limits(r, &t->limits, true);
}

static void globaltype(wasm_reader *r, wasm_globaltype *g) {
    g->type = valtype(r);
    size_t at = wasm_rd_offset(r);
    uint8_t m = wasm_rd_u8(r);
    if (!r->failed && m > 1) wasm_rd_fail_at(r, at, "malformed mutability");
    g->mut = m == 1;
}

/* A constant expression: instructions up to the `end` that closes it (constant expressions have
 * no blocks, so the first `end` does). Which instructions are constant is the validator's. */
static void expr(wasm_reader *r, wasm_expr *e) {
    e->start = r->p;
    e->offset = wasm_rd_offset(r);
    wasm_instr in;
    for (;;) {
        if (!wasm_rd_instr(r, &in)) return;
        if (in.op == WASM_OP_END) break;
    }
    e->len = (size_t)(r->p - e->start);
}

static bool types(wasm_reader *r, wasm_module *m) {
    m->ntypes = wasm_rd_count(r, 3, "types");
    if (!(m->types = alloc(m->ntypes, sizeof *m->types))) return oom(r);
    for (uint32_t i = 0; i < m->ntypes && !r->failed; i++) {
        size_t at = wasm_rd_offset(r);
        uint8_t form = wasm_rd_u8(r);
        if (r->failed) break;
        if (form != WASM_FUNC_FORM) return wasm_rd_fail_at(r, at, "invalid leading byte in type definition (0x%x)", form);
        wasm_functype *t = &m->types[i];
        t->nparams = wasm_rd_count(r, 1, "function params");
        t->params = r->p;
        for (uint32_t k = 0; k < t->nparams && !r->failed; k++) valtype(r);
        t->nresults = wasm_rd_count(r, 1, "function results");
        t->results = r->p;
        for (uint32_t k = 0; k < t->nresults && !r->failed; k++) valtype(r);
    }
    return !r->failed;
}

static bool imports(wasm_reader *r, wasm_module *m) {
    m->nimports = wasm_rd_count(r, 4, "imports");
    if (!(m->imports = alloc(m->nimports, sizeof *m->imports))) return oom(r);
    for (uint32_t i = 0; i < m->nimports && !r->failed; i++) {
        wasm_import *im = &m->imports[i];
        im->module = wasm_rd_name(r, &im->module_len);
        im->name = wasm_rd_name(r, &im->name_len);
        size_t at = wasm_rd_offset(r);
        im->kind = wasm_rd_u8(r);
        if (r->failed) break;
        switch (im->kind) {
        case WASM_EXTERN_FUNC: im->func_type = wasm_rd_u32(r); m->nfunc_imports++; break;
        case WASM_EXTERN_TABLE: tabletype(r, &im->table); m->ntable_imports++; break;
        case WASM_EXTERN_MEMORY: limits(r, &im->mem, false); m->nmem_imports++; break;
        case WASM_EXTERN_GLOBAL: globaltype(r, &im->global); m->nglobal_imports++; break;
        case WASM_EXTERN_TAG: return wasm_rd_fail_at(r, at, "exceptions proposal not enabled");
        default: return wasm_rd_fail_at(r, at, "invalid leading byte (0x%x) for external kind", im->kind);
        }
    }
    return !r->failed;
}

static bool functions(wasm_reader *r, wasm_module *m, uint32_t *ndefined) {
    *ndefined = wasm_rd_count(r, 1, "functions");
    m->nfuncs = m->nfunc_imports + *ndefined;
    if (!(m->funcs = alloc(m->nfuncs, sizeof *m->funcs))) return oom(r);
    uint32_t f = 0;
    for (uint32_t i = 0; i < m->nimports; i++)
        if (m->imports[i].kind == WASM_EXTERN_FUNC) m->funcs[f++] = m->imports[i].func_type;
    for (uint32_t i = 0; i < *ndefined && !r->failed; i++) m->funcs[f++] = wasm_rd_u32(r);
    return !r->failed;
}

/* No function section: the imported functions alone. */
static bool functions_none(wasm_reader *r, wasm_module *m) {
    m->nfuncs = m->nfunc_imports;
    if (!(m->funcs = alloc(m->nfuncs, sizeof *m->funcs))) return oom(r);
    uint32_t f = 0;
    for (uint32_t i = 0; i < m->nimports; i++)
        if (m->imports[i].kind == WASM_EXTERN_FUNC) m->funcs[f++] = m->imports[i].func_type;
    return true;
}

static bool tables(wasm_reader *r, wasm_module *m, uint32_t n) {
    m->ntables = m->ntable_imports + n;
    if (!(m->tables = alloc(m->ntables, sizeof *m->tables))) return oom(r);
    uint32_t t = 0;
    for (uint32_t i = 0; i < m->nimports; i++)
        if (m->imports[i].kind == WASM_EXTERN_TABLE) m->tables[t++] = m->imports[i].table;
    for (uint32_t i = 0; i < n && !r->failed; i++) tabletype(r, &m->tables[t++]);
    return !r->failed;
}

static bool memories(wasm_reader *r, wasm_module *m, uint32_t n) {
    m->nmems = m->nmem_imports + n;
    if (!(m->mems = alloc(m->nmems, sizeof *m->mems))) return oom(r);
    uint32_t k = 0;
    for (uint32_t i = 0; i < m->nimports; i++)
        if (m->imports[i].kind == WASM_EXTERN_MEMORY) m->mems[k++] = m->imports[i].mem;
    for (uint32_t i = 0; i < n && !r->failed; i++) limits(r, &m->mems[k++], false);
    return !r->failed;
}

static bool globals(wasm_reader *r, wasm_module *m, uint32_t n) {
    m->nglobals = m->nglobal_imports + n;
    if (!(m->globals = alloc(m->nglobals, sizeof *m->globals)) || !(m->global_inits = alloc(n, sizeof *m->global_inits)))
        return oom(r);
    uint32_t g = 0;
    for (uint32_t i = 0; i < m->nimports; i++)
        if (m->imports[i].kind == WASM_EXTERN_GLOBAL) m->globals[g++] = m->imports[i].global;
    for (uint32_t i = 0; i < n && !r->failed; i++) {
        globaltype(r, &m->globals[g++]);
        expr(r, &m->global_inits[i]);
    }
    return !r->failed;
}

static bool exports(wasm_reader *r, wasm_module *m) {
    m->nexports = wasm_rd_count(r, 3, "exports");
    if (!(m->exports = alloc(m->nexports, sizeof *m->exports))) return oom(r);
    for (uint32_t i = 0; i < m->nexports && !r->failed; i++) {
        wasm_export *e = &m->exports[i];
        e->name = wasm_rd_name(r, &e->name_len);
        size_t at = wasm_rd_offset(r);
        e->kind = wasm_rd_u8(r);
        if (!r->failed && e->kind > WASM_EXTERN_GLOBAL)
            return wasm_rd_fail_at(r, at, "invalid leading byte (0x%x) for external kind", e->kind);
        e->index = wasm_rd_u32(r);
    }
    return !r->failed;
}

static bool elements(wasm_reader *r, wasm_module *m) {
    m->nelems = wasm_rd_count(r, 2, "element segments");
    if (!(m->elems = alloc(m->nelems, sizeof *m->elems))) return oom(r);
    for (uint32_t i = 0; i < m->nelems && !r->failed; i++) {
        wasm_elem *e = &m->elems[i];
        e->offset_in_binary = wasm_rd_offset(r);
        uint32_t flags = wasm_rd_u32(r);
        if (r->failed) break;
        if (flags > 7) return wasm_rd_fail_at(r, e->offset_in_binary, "invalid flags byte in element segment");
        /* bit 0: passive or declarative (bit 1 then says which); bit 1 on an active one: a table
         * index; bit 2: expressions instead of function indices. */
        e->mode = !(flags & 1) ? WASM_SEG_ACTIVE : (flags & 2) ? WASM_SEG_DECLARATIVE : WASM_SEG_PASSIVE;
        if (e->mode == WASM_SEG_ACTIVE) {
            e->table = (flags & 2) ? wasm_rd_u32(r) : 0;
            expr(r, &e->offset);
        }
        bool exprs = flags & 4;
        e->type = WASM_FUNCREF;
        if (flags & 3) {
            size_t at = wasm_rd_offset(r);
            uint8_t k = wasm_rd_u8(r);
            if (r->failed) break;
            if (exprs) {
                if (!wasm_is_ref(k)) return wasm_rd_fail_at(r, at, "malformed reference type");
                e->type = k;
            } else if (k != 0x00) {
                return wasm_rd_fail_at(r, at, "invalid external kind in element segment");
            }
        }
        e->count = wasm_rd_count(r, 1, "elements");
        if (r->failed) break;
        if (exprs) {
            if (!(e->exprs = alloc(e->count, sizeof *e->exprs))) return oom(r);
            for (uint32_t k = 0; k < e->count && !r->failed; k++) expr(r, &e->exprs[k]);
        } else {
            if (!(e->funcs = alloc(e->count, sizeof *e->funcs))) return oom(r);
            for (uint32_t k = 0; k < e->count && !r->failed; k++) e->funcs[k] = wasm_rd_u32(r);
        }
    }
    return !r->failed;
}

static bool codes(wasm_reader *r, wasm_module *m, uint32_t ndefined) {
    size_t at = wasm_rd_offset(r);
    m->ncodes = wasm_rd_count(r, 1, "function bodies");
    if (r->failed) return false;
    if (m->ncodes != ndefined) return wasm_rd_fail_at(r, at, "function and code section have inconsistent lengths");
    if (!(m->codes = alloc(m->ncodes, sizeof *m->codes))) return oom(r);
    for (uint32_t i = 0; i < m->ncodes && !r->failed; i++) {
        wasm_code *c = &m->codes[i];
        c->offset = wasm_rd_offset(r);
        uint32_t size = wasm_rd_u32(r);
        const uint8_t *body = wasm_rd_bytes(r, size);
        if (!body) break;
        wasm_reader b = *r;
        b.p = body;
        b.end = body + size;
        c->nlocal_groups = wasm_rd_count(&b, 2, "local declarations");
        if (!b.failed && !(c->locals = alloc(c->nlocal_groups, sizeof *c->locals))) return oom(r);
        for (uint32_t k = 0; k < c->nlocal_groups && !b.failed; k++) {
            size_t lat = wasm_rd_offset(&b);
            c->locals[k].count = wasm_rd_u32(&b);
            c->locals[k].type = valtype(&b);
            c->nlocals += c->locals[k].count;
            if (!b.failed && c->nlocals > WASM_MAX_LOCALS) wasm_rd_fail_at(&b, lat, "too many locals");
        }
        if (b.failed) {
            *r = b;
            break;
        }
        c->body.start = b.p;
        c->body.len = (size_t)(b.end - b.p);
        c->body.offset = wasm_rd_offset(&b);
        if (c->body.len == 0) return wasm_rd_fail_at(r, c->body.offset, "unexpected end-of-file");
    }
    return !r->failed;
}

static bool datas(wasm_reader *r, wasm_module *m) {
    size_t at = wasm_rd_offset(r);
    m->ndatas = wasm_rd_count(r, 2, "data segments");
    if (r->failed) return false;
    if (m->has_datacount && m->ndatas != m->datacount)
        return wasm_rd_fail_at(r, at, "data count and data section have inconsistent lengths");
    if (!(m->datas = alloc(m->ndatas, sizeof *m->datas))) return oom(r);
    for (uint32_t i = 0; i < m->ndatas && !r->failed; i++) {
        wasm_data *d = &m->datas[i];
        d->offset_in_binary = wasm_rd_offset(r);
        uint32_t flags = wasm_rd_u32(r);
        if (r->failed) break;
        if (flags > 2) return wasm_rd_fail_at(r, d->offset_in_binary, "invalid flags byte in data segment");
        d->mode = flags == 1 ? WASM_SEG_PASSIVE : WASM_SEG_ACTIVE;
        if (flags == 2) d->mem = wasm_rd_u32(r);
        if (d->mode == WASM_SEG_ACTIVE) expr(r, &d->offset);
        d->len = wasm_rd_u32(r);
        d->bytes = wasm_rd_bytes(r, d->len);
    }
    return !r->failed;
}

/* Where a section may appear: its rank in the order the spec fixes (custom sections anywhere). */
static int rank(uint8_t id) {
    switch (id) {
    case WASM_SEC_TYPE: return 1;
    case WASM_SEC_IMPORT: return 2;
    case WASM_SEC_FUNCTION: return 3;
    case WASM_SEC_TABLE: return 4;
    case WASM_SEC_MEMORY: return 5;
    case WASM_SEC_TAG: return 6;
    case WASM_SEC_GLOBAL: return 7;
    case WASM_SEC_EXPORT: return 8;
    case WASM_SEC_START: return 9;
    case WASM_SEC_ELEMENT: return 10;
    case WASM_SEC_DATACOUNT: return 11;
    case WASM_SEC_CODE: return 12;
    case WASM_SEC_DATA: return 13;
    default: return -1;
    }
}

bool wasm_decode(const uint8_t *bytes, size_t len, wasm_module *m, char *err, size_t errlen) {
    memset(m, 0, sizeof *m);
    m->bytes = bytes;
    m->len = len;
    wasm_reader r = wasm_reader_at(bytes, bytes, bytes + len, err, errlen);
    const uint8_t *magic = wasm_rd_bytes(&r, 4);
    if (!magic || memcmp(magic, "\0asm", 4) != 0) {
        if (!r.failed) wasm_rd_fail_at(&r, 0, "magic header not detected: bad magic number");
        goto fail;
    }
    uint32_t version = wasm_rd_le32(&r);
    if (r.failed) goto fail;
    if (version != 1) {
        wasm_rd_fail_at(&r, 4, "unknown binary version: 0x%x", version);
        goto fail;
    }
    int last = 0;
    uint32_t ndefined = 0, ntables = 0, nmems = 0, nglobals = 0;
    bool have_funcs = false;
    bool tables_done = false, mems_done = false, globals_done = false;
    while (!wasm_rd_eof(&r) && !r.failed) {
        size_t sec_at = wasm_rd_offset(&r);
        uint8_t id = wasm_rd_u8(&r);
        uint32_t size = wasm_rd_u32(&r);
        const uint8_t *body = wasm_rd_bytes(&r, size);
        if (!body) break;
        wasm_reader s = r;
        s.p = body;
        s.end = body + size;
        if (id == WASM_SEC_CUSTOM) {
            uint32_t n;
            wasm_rd_name(&s, &n);
            if (s.failed) r = s;
            continue;
        }
        int k = rank(id);
        if (k < 0) {
            wasm_rd_fail_at(&r, sec_at, "malformed section id: %u", id);
            break;
        }
        if (k <= last) {
            wasm_rd_fail_at(&r, sec_at, "section out of order");
            break;
        }
        last = k;
        switch (id) {
        case WASM_SEC_TYPE: types(&s, m); break;
        case WASM_SEC_IMPORT: imports(&s, m); break;
        case WASM_SEC_FUNCTION: functions(&s, m, &ndefined); have_funcs = true; break;
        case WASM_SEC_TABLE:
            ntables = wasm_rd_count(&s, 3, "tables");
            tables(&s, m, ntables);
            tables_done = true;
            break;
        case WASM_SEC_MEMORY:
            nmems = wasm_rd_count(&s, 2, "memories");
            memories(&s, m, nmems);
            mems_done = true;
            break;
        case WASM_SEC_TAG: wasm_rd_fail_at(&s, sec_at, "exceptions proposal not enabled"); break;
        case WASM_SEC_GLOBAL:
            nglobals = wasm_rd_count(&s, 3, "globals");
            globals(&s, m, nglobals);
            globals_done = true;
            break;
        case WASM_SEC_EXPORT: exports(&s, m); break;
        case WASM_SEC_START:
            m->has_start = true;
            m->start = wasm_rd_u32(&s);
            break;
        case WASM_SEC_ELEMENT: elements(&s, m); break;
        case WASM_SEC_DATACOUNT:
            m->has_datacount = true;
            m->datacount = wasm_rd_u32(&s);
            break;
        case WASM_SEC_CODE:
            m->code_section_offset = sec_at;
            codes(&s, m, ndefined);
            break;
        case WASM_SEC_DATA: datas(&s, m); break;
        }
        if (s.failed) {
            r = s;
            break;
        }
        if (!wasm_rd_eof(&s)) {
            wasm_rd_fail_at(&r, wasm_rd_offset(&s), "section size mismatch: unexpected data at the end of the section");
            break;
        }
    }
    if (r.failed) goto fail;
    /* Absent sections: the index spaces still hold the imports. */
    {
        wasm_reader z = wasm_reader_at(bytes, bytes + len, bytes + len, err, errlen);
        if (!have_funcs && !functions_none(&z, m)) goto oomfail;
        if (!tables_done && !tables(&z, m, 0)) goto oomfail;
        if (!mems_done && !memories(&z, m, 0)) goto oomfail;
        if (!globals_done && !globals(&z, m, 0)) goto oomfail;
    }
    if (ndefined && !m->codes) {
        wasm_rd_fail_at(&r, len, "function and code section have inconsistent lengths");
        goto fail;
    }
    if (m->has_datacount && m->datacount && !m->datas) {
        wasm_rd_fail_at(&r, len, "data count and data section have inconsistent lengths");
        goto fail;
    }
    return true;
oomfail:
    if (!r.failed) wasm_rd_fail_at(&r, len, "out of memory");
fail:
    wasm_module_free(m);
    return false;
}
