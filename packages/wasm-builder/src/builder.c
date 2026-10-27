/* Byte encoders and the module assembler. */
#include "wasm/builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *grow(void *p, size_t n, size_t size) {
    void *q = realloc(p, (n ? n : 1) * size);
    if (!q) {
        fputs("wasm-builder: out of memory\n", stderr);
        abort();
    }
    return q;
}

void wasm_buf_free(wasm_buf *b) {
    free(b->data);
    memset(b, 0, sizeof *b);
}

void wb_bytes(wasm_buf *b, const void *p, size_t n) {
    if (b->len + n > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        while (b->cap < b->len + n) b->cap *= 2;
        b->data = grow(b->data, b->cap, 1);
    }
    if (n) memcpy(b->data + b->len, p, n);
    b->len += n;
}

void wb_byte(wasm_buf *b, uint8_t v) { wb_bytes(b, &v, 1); }

void wb_u64(wasm_buf *b, uint64_t v) {
    do {
        uint8_t x = v & 0x7f;
        v >>= 7;
        wb_byte(b, (uint8_t)(x | (v ? 0x80 : 0)));
    } while (v);
}

void wb_u32(wasm_buf *b, uint32_t v) { wb_u64(b, v); }

void wb_s64(wasm_buf *b, int64_t v) {
    for (;;) {
        uint8_t x = (uint8_t)(v & 0x7f);
        /* An arithmetic shift, written out so it doesn't depend on the implementation. */
        v = v < 0 ? ~(~v >> 7) : v >> 7;
        if ((v == 0 && !(x & 0x40)) || (v == -1 && (x & 0x40))) {
            wb_byte(b, x);
            return;
        }
        wb_byte(b, (uint8_t)(x | 0x80));
    }
}

void wb_s32(wasm_buf *b, int32_t v) { wb_s64(b, v); }

void wb_f32(wasm_buf *b, float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    uint8_t le[4] = {(uint8_t)u, (uint8_t)(u >> 8), (uint8_t)(u >> 16), (uint8_t)(u >> 24)};
    wb_bytes(b, le, 4);
}

void wb_f64(wasm_buf *b, double v) {
    uint64_t u;
    memcpy(&u, &v, 8);
    for (int i = 0; i < 8; i++) wb_byte(b, (uint8_t)(u >> (8 * i)));
}

void wb_name(wasm_buf *b, const char *s) {
    size_t n = strlen(s);
    wb_u32(b, (uint32_t)n);
    wb_bytes(b, s, n);
}

void wb_section(wasm_buf *b, uint8_t id, const wasm_buf *payload) {
    wb_byte(b, id);
    wb_u32(b, (uint32_t)payload->len);
    wb_bytes(b, payload->data, payload->len);
}

void wb_i32_const(wasm_buf *b, int32_t v) {
    wb_byte(b, 0x41);
    wb_s32(b, v);
}

void wb_i64_const(wasm_buf *b, int64_t v) {
    wb_byte(b, 0x42);
    wb_s64(b, v);
}

void wb_op(wasm_buf *b, uint8_t op) { wb_byte(b, op); }

void wb_op_u32(wasm_buf *b, uint8_t op, uint32_t a) {
    wb_byte(b, op);
    wb_u32(b, a);
}

void wb_fc(wasm_buf *b, uint32_t sub) {
    wb_byte(b, 0xfc);
    wb_u32(b, sub);
}

void wb_memarg(wasm_buf *b, uint8_t op, uint32_t align, uint32_t offset) {
    wb_byte(b, op);
    wb_u32(b, align);
    wb_u32(b, offset);
}

static wasm_valtype letter(char c) {
    switch (c) {
    case 'i': return WASM_I32;
    case 'l': return WASM_I64;
    case 'f': return WASM_F32;
    case 'd': return WASM_F64;
    case 'F': return WASM_FUNCREF;
    case 'E': return WASM_EXTERNREF;
    case 'V': return WASM_V128;
    default: return 0;
    }
}

void wasm_builder_free(wasm_builder *w) {
    wasm_buf_free(&w->types);
    for (uint32_t i = 0; i < w->nimports; i++) {
        free(w->imports[i].module);
        free(w->imports[i].name);
    }
    free(w->imports);
    for (uint32_t i = 0; i < w->nfuncs; i++) {
        free(w->funcs[i].locals);
        wasm_buf_free(&w->funcs[i].body);
    }
    free(w->funcs);
    for (uint32_t i = 0; i < w->nglobals; i++) wasm_buf_free(&w->globals[i].init);
    free(w->globals);
    for (uint32_t i = 0; i < w->nexports; i++) free(w->exports[i].name);
    free(w->exports);
    for (uint32_t i = 0; i < w->nelems; i++) free(w->elems[i].funcs);
    free(w->elems);
    for (uint32_t i = 0; i < w->ndatas; i++) wasm_buf_free(&w->datas[i].bytes);
    free(w->datas);
    memset(w, 0, sizeof *w);
}

static char *dup(const char *s) {
    char *d = grow(NULL, strlen(s) + 1, 1);
    memcpy(d, s, strlen(s) + 1);
    return d;
}

uint32_t wasm_builder_type(wasm_builder *w, const char *sig) {
    const char *gt = strchr(sig, '>');
    size_t np = gt ? (size_t)(gt - sig) : strlen(sig);
    wb_byte(&w->types, WASM_FUNC_FORM);
    wb_u32(&w->types, (uint32_t)np);
    for (size_t i = 0; i < np; i++) wb_byte(&w->types, letter(sig[i]));
    const char *res = gt ? gt + 1 : "";
    wb_u32(&w->types, (uint32_t)strlen(res));
    for (const char *c = res; *c; c++) wb_byte(&w->types, letter(*c));
    return w->ntypes++;
}

static wasm_builder_import *import(wasm_builder *w, const char *module, const char *name, uint8_t kind) {
    w->imports = grow(w->imports, w->nimports + 1, sizeof *w->imports);
    wasm_builder_import *im = &w->imports[w->nimports++];
    memset(im, 0, sizeof *im);
    im->module = dup(module);
    im->name = dup(name);
    im->kind = kind;
    return im;
}

uint32_t wasm_builder_import_func(wasm_builder *w, const char *module, const char *name, uint32_t type) {
    import(w, module, name, WASM_EXTERN_FUNC)->type = type;
    return w->nfunc_imports++;
}

void wasm_builder_import_memory(wasm_builder *w, const char *module, const char *name, uint32_t min) {
    import(w, module, name, WASM_EXTERN_MEMORY)->min = min;
}

wasm_buf *wasm_builder_func_add(wasm_builder *w, uint32_t type, const char *locals, uint32_t *index) {
    w->funcs = grow(w->funcs, w->nfuncs + 1, sizeof *w->funcs);
    wasm_builder_func *f = &w->funcs[w->nfuncs];
    memset(f, 0, sizeof *f);
    f->type = type;
    f->nlocals = (uint32_t)strlen(locals);
    f->locals = grow(NULL, f->nlocals, 1);
    for (uint32_t i = 0; i < f->nlocals; i++) f->locals[i] = letter(locals[i]);
    if (index) *index = w->nfunc_imports + w->nfuncs;
    w->nfuncs++;
    return &f->body;
}

void wasm_builder_memory(wasm_builder *w, uint32_t min, uint32_t max, bool has_max) {
    w->has_memory = true;
    w->mem_min = min;
    w->mem_max = max;
    w->mem_has_max = has_max;
}

void wasm_builder_table(wasm_builder *w, uint32_t min, uint32_t max, bool has_max) {
    w->has_table = true;
    w->table_min = min;
    w->table_max = max;
    w->table_has_max = has_max;
}

wasm_buf *wasm_builder_add_global(wasm_builder *w, wasm_valtype type, bool mut) {
    w->globals = grow(w->globals, w->nglobals + 1, sizeof *w->globals);
    wasm_builder_global *g = &w->globals[w->nglobals++];
    memset(g, 0, sizeof *g);
    g->type = type;
    g->mut = mut;
    return &g->init;
}

void wasm_builder_add_export(wasm_builder *w, const char *name, uint8_t kind, uint32_t index) {
    w->exports = grow(w->exports, w->nexports + 1, sizeof *w->exports);
    w->exports[w->nexports++] = (wasm_builder_export){dup(name), kind, index};
}

void wasm_builder_data(wasm_builder *w, uint32_t offset, const void *bytes, size_t n) {
    w->datas = grow(w->datas, w->ndatas + 1, sizeof *w->datas);
    wasm_builder_segment *d = &w->datas[w->ndatas++];
    memset(d, 0, sizeof *d);
    d->offset = offset;
    wb_bytes(&d->bytes, bytes, n);
}

void wasm_builder_data_passive(wasm_builder *w, const void *bytes, size_t n) {
    wasm_builder_data(w, 0, bytes, n);
    w->datas[w->ndatas - 1].passive = true;
    w->datacount = true;
}

void wasm_builder_elem(wasm_builder *w, uint32_t offset, const uint32_t *funcs, uint32_t n) {
    w->elems = grow(w->elems, w->nelems + 1, sizeof *w->elems);
    wasm_builder_segment *e = &w->elems[w->nelems++];
    memset(e, 0, sizeof *e);
    e->offset = offset;
    e->funcs = grow(NULL, n, sizeof *funcs);
    if (n) memcpy(e->funcs, funcs, n * sizeof *funcs);
    e->nfuncs = n;
}

static void limits(wasm_buf *b, uint32_t min, uint32_t max, bool has_max) {
    wb_byte(b, has_max ? 1 : 0);
    wb_u32(b, min);
    if (has_max) wb_u32(b, max);
}

void wasm_builder_finish(const wasm_builder *w, wasm_buf *out) {
    memset(out, 0, sizeof *out);
    wb_bytes(out, "\0asm\x01\0\0\0", 8);
    wasm_buf s = {0};
    if (w->ntypes) {
        wb_u32(&s, w->ntypes);
        wb_bytes(&s, w->types.data, w->types.len);
        wb_section(out, WASM_SEC_TYPE, &s);
    }
    if (w->nimports) {
        s.len = 0;
        wb_u32(&s, w->nimports);
        for (uint32_t i = 0; i < w->nimports; i++) {
            const wasm_builder_import *im = &w->imports[i];
            wb_name(&s, im->module);
            wb_name(&s, im->name);
            wb_byte(&s, im->kind);
            if (im->kind == WASM_EXTERN_FUNC) wb_u32(&s, im->type);
            else if (im->kind == WASM_EXTERN_MEMORY) limits(&s, im->min, im->max, im->has_max);
        }
        wb_section(out, WASM_SEC_IMPORT, &s);
    }
    if (w->nfuncs) {
        s.len = 0;
        wb_u32(&s, w->nfuncs);
        for (uint32_t i = 0; i < w->nfuncs; i++) wb_u32(&s, w->funcs[i].type);
        wb_section(out, WASM_SEC_FUNCTION, &s);
    }
    if (w->has_table) {
        s.len = 0;
        wb_u32(&s, 1);
        wb_byte(&s, WASM_FUNCREF);
        limits(&s, w->table_min, w->table_max, w->table_has_max);
        wb_section(out, WASM_SEC_TABLE, &s);
    }
    if (w->has_memory) {
        s.len = 0;
        wb_u32(&s, 1);
        limits(&s, w->mem_min, w->mem_max, w->mem_has_max);
        wb_section(out, WASM_SEC_MEMORY, &s);
    }
    if (w->nglobals) {
        s.len = 0;
        wb_u32(&s, w->nglobals);
        for (uint32_t i = 0; i < w->nglobals; i++) {
            wb_byte(&s, w->globals[i].type);
            wb_byte(&s, w->globals[i].mut);
            wb_bytes(&s, w->globals[i].init.data, w->globals[i].init.len);
            wb_byte(&s, 0x0b);
        }
        wb_section(out, WASM_SEC_GLOBAL, &s);
    }
    if (w->nexports) {
        s.len = 0;
        wb_u32(&s, w->nexports);
        for (uint32_t i = 0; i < w->nexports; i++) {
            wb_name(&s, w->exports[i].name);
            wb_byte(&s, w->exports[i].kind);
            wb_u32(&s, w->exports[i].index);
        }
        wb_section(out, WASM_SEC_EXPORT, &s);
    }
    if (w->has_start) {
        s.len = 0;
        wb_u32(&s, w->start);
        wb_section(out, WASM_SEC_START, &s);
    }
    if (w->nelems) {
        s.len = 0;
        wb_u32(&s, w->nelems);
        for (uint32_t i = 0; i < w->nelems; i++) {
            wb_u32(&s, 0);
            wb_i32_const(&s, (int32_t)w->elems[i].offset);
            wb_byte(&s, 0x0b);
            wb_u32(&s, w->elems[i].nfuncs);
            for (uint32_t k = 0; k < w->elems[i].nfuncs; k++) wb_u32(&s, w->elems[i].funcs[k]);
        }
        wb_section(out, WASM_SEC_ELEMENT, &s);
    }
    if (w->datacount) {
        s.len = 0;
        wb_u32(&s, w->ndatas);
        wb_section(out, WASM_SEC_DATACOUNT, &s);
    }
    if (w->nfuncs) {
        s.len = 0;
        wb_u32(&s, w->nfuncs);
        for (uint32_t i = 0; i < w->nfuncs; i++) {
            const wasm_builder_func *f = &w->funcs[i];
            wasm_buf e = {0};
            /* Locals, run-length encoded. */
            uint32_t groups = 0;
            for (uint32_t k = 0; k < f->nlocals; k++)
                if (k == 0 || f->locals[k] != f->locals[k - 1]) groups++;
            wb_u32(&e, groups);
            for (uint32_t k = 0; k < f->nlocals;) {
                uint32_t n = 1;
                while (k + n < f->nlocals && f->locals[k + n] == f->locals[k]) n++;
                wb_u32(&e, n);
                wb_byte(&e, f->locals[k]);
                k += n;
            }
            wb_bytes(&e, f->body.data, f->body.len);
            wb_byte(&e, 0x0b);
            wb_u32(&s, (uint32_t)e.len);
            wb_bytes(&s, e.data, e.len);
            wasm_buf_free(&e);
        }
        wb_section(out, WASM_SEC_CODE, &s);
    }
    if (w->ndatas) {
        s.len = 0;
        wb_u32(&s, w->ndatas);
        for (uint32_t i = 0; i < w->ndatas; i++) {
            const wasm_builder_segment *d = &w->datas[i];
            if (d->passive) {
                wb_u32(&s, 1);
            } else {
                wb_u32(&s, 0);
                wb_i32_const(&s, (int32_t)d->offset);
                wb_byte(&s, 0x0b);
            }
            wb_u32(&s, (uint32_t)d->bytes.len);
            wb_bytes(&s, d->bytes.data, d->bytes.len);
        }
        wb_section(out, WASM_SEC_DATA, &s);
    }
    wasm_buf_free(&s);
}
