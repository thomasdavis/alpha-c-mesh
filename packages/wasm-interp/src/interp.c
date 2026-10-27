/* Compiling, instantiating, and the run loop. */
#include "wasm/interp.h"

#include "wasm/decode.h"
#include "wasm/instr.h"
#include "wasm/lower.h"
#include "wasm/reader.h"
#include "wasm/validate.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "linear memory is accessed in place: a little-endian host");

struct wasm_compiled {
    uint8_t *bytes;
    size_t len;
    wasm_module m;
    wasm_fn_code *fns; /* defined functions */
    uint32_t *canon;   /* canonical type ids */
};

typedef struct {
    uint64_t *elems; /* function index + 1, or 0 for null; externrefs as given */
    uint32_t size;
    uint64_t max;
} table;

typedef struct {
    const wasm_fn_code *fn;
    const wasm_ins *pc; /* where to continue in the caller */
    uint64_t *fp;
} frame;

struct wasm_instance {
    const wasm_compiled *c;
    const wasm_host_func **hosts; /* per imported function */
    uint8_t *mem;
    uint64_t mem_size;
    uint64_t mem_max; /* bytes: the module's maximum and the store's limit */
    bool has_mem;
    uint64_t *globals;
    table *tables;
    uint32_t ntables;
    uint64_t table_limit;
    uint64_t **elem_values; /* each element segment's references; NULL once dropped */
    uint32_t *elem_len;
    bool *data_dropped;
    uint64_t *stack;
    uint32_t stack_slots;
    frame *frames;
    uint32_t max_frames;
    const _Atomic uint64_t *epoch;
    uint64_t deadline;
    void *user;
};

static void say(char *err, size_t errlen, const char *fmt, ...) {
    if (!err || !errlen) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

/* ---- compiling ---------------------------------------------------------------------------- */

void wasm_compiled_free(wasm_compiled *c) {
    if (!c) return;
    if (c->fns)
        for (uint32_t i = 0; i < c->m.ncodes; i++) wasm_fn_code_free(&c->fns[i]);
    free(c->fns);
    free(c->canon);
    wasm_module_free(&c->m);
    free(c->bytes);
    free(c);
}

const wasm_module *wasm_compiled_module(const wasm_compiled *c) { return &c->m; }

wasm_compiled *wasm_compile(const uint8_t *bytes, size_t len, wasm_features features, char *err, size_t errlen) {
    wasm_compiled *c = calloc(1, sizeof *c);
    if (!c || !(c->bytes = malloc(len ? len : 1))) {
        free(c);
        say(err, errlen, "out of memory");
        return NULL;
    }
    if (len) memcpy(c->bytes, bytes, len);
    c->len = len;
    if (!wasm_decode(c->bytes, len, &c->m, err, errlen) || !wasm_validate(&c->m, features, NULL, err, errlen)) goto fail;
    const wasm_module *m = &c->m;
    if (m->nmems > 1) {
        say(err, errlen, "multi-memory is not supported by this executor");
        goto fail;
    }
    if (m->nmems && (m->mems[0].flags & (WASM_LIMITS_64 | WASM_LIMITS_SHARED))) {
        say(err, errlen, "%s memories are not supported by this executor",
            (m->mems[0].flags & WASM_LIMITS_64) ? "64-bit" : "shared");
        goto fail;
    }
    if (!(c->canon = wasm_canonical_types(m)) || !(c->fns = calloc(m->ncodes ? m->ncodes : 1, sizeof *c->fns))) {
        say(err, errlen, "out of memory");
        goto fail;
    }
    for (uint32_t i = 0; i < m->ncodes; i++)
        if (!wasm_lower(m, m->nfunc_imports + i, &c->fns[i], err, errlen)) goto fail;
    return c;
fail:
    wasm_compiled_free(c);
    return NULL;
}

/* ---- instantiating ------------------------------------------------------------------------ */

static bool sig_matches(const wasm_functype *t, const char *sig) {
    const char *gt = strchr(sig, '>');
    if (!gt || (size_t)(gt - sig) != t->nparams || strlen(gt + 1) != t->nresults) return false;
    for (uint32_t i = 0; i < t->nparams; i++)
        if (wasm_sig_type(sig[i]) != t->params[i]) return false;
    for (uint32_t i = 0; i < t->nresults; i++)
        if (wasm_sig_type(gt[1 + i]) != t->results[i]) return false;
    return true;
}

static bool name_is(const uint8_t *s, uint32_t n, const char *z) { return strlen(z) == n && memcmp(s, z, n) == 0; }

/* A constant expression's value (validated: the ops are constant, the types right). */
static uint64_t eval_const(const wasm_instance *I, const wasm_expr *e) {
    wasm_reader r = wasm_reader_at(e->start, e->start, e->start + e->len, NULL, 0);
    uint64_t stack[64];
    uint32_t sp = 0;
    wasm_instr in;
    while (wasm_rd_instr(&r, &in) && in.op != WASM_OP_END && sp < 64) {
        switch (in.op) {
        case WASM_OP_I32_CONST: stack[sp++] = (uint32_t)in.i; break;
        case WASM_OP_I64_CONST: stack[sp++] = (uint64_t)in.i; break;
        case WASM_OP_F32_CONST: stack[sp++] = in.f32; break;
        case WASM_OP_F64_CONST: stack[sp++] = in.f64; break;
        case WASM_OP_REF_NULL: stack[sp++] = 0; break;
        case WASM_OP_REF_FUNC: stack[sp++] = (uint64_t)in.a + 1; break;
        case WASM_OP_GLOBAL_GET: stack[sp++] = I->globals[in.a]; break;
        case 0x6a: sp--; stack[sp - 1] = (uint32_t)(stack[sp - 1] + stack[sp]); break;
        case 0x6b: sp--; stack[sp - 1] = (uint32_t)(stack[sp - 1] - stack[sp]); break;
        case 0x6c: sp--; stack[sp - 1] = (uint32_t)((uint32_t)stack[sp - 1] * (uint32_t)stack[sp]); break;
        case 0x7c: sp--; stack[sp - 1] = stack[sp - 1] + stack[sp]; break;
        case 0x7d: sp--; stack[sp - 1] = stack[sp - 1] - stack[sp]; break;
        case 0x7e: sp--; stack[sp - 1] = stack[sp - 1] * stack[sp]; break;
        default: break;
        }
    }
    return sp ? stack[sp - 1] : 0;
}

void wasm_instance_free(wasm_instance *I) {
    if (!I) return;
    free(I->hosts);
    free(I->mem);
    free(I->globals);
    if (I->tables)
        for (uint32_t i = 0; i < I->ntables; i++) free(I->tables[i].elems);
    free(I->tables);
    if (I->elem_values)
        for (uint32_t i = 0; i < I->c->m.nelems; i++) free(I->elem_values[i]);
    free(I->elem_values);
    free(I->elem_len);
    free(I->data_dropped);
    free(I->stack);
    free(I->frames);
    free(I);
}

wasm_instance *wasm_instantiate(const wasm_compiled *c, const wasm_host_func *imports, size_t nimports,
                                const wasm_store_limits *lim, void *user, char *err, size_t errlen) {
    const wasm_module *m = &c->m;
    wasm_store_limits L = lim ? *lim : (wasm_store_limits){0};
    wasm_instance *I = calloc(1, sizeof *I);
    if (!I) goto oom;
    I->c = c;
    I->user = user;
    I->stack_slots = L.stack_slots ? L.stack_slots : 128u << 10;
    I->max_frames = L.call_depth ? L.call_depth : 10000;
    I->table_limit = WASM_MAX_TABLE;
    if (!(I->hosts = calloc(m->nfunc_imports ? m->nfunc_imports : 1, sizeof *I->hosts))) goto oom;
    uint32_t f = 0;
    for (uint32_t i = 0; i < m->nimports; i++) {
        const wasm_import *im = &m->imports[i];
        if (im->kind != WASM_EXTERN_FUNC) {
            say(err, errlen, "unknown import: `%.*s::%.*s` has not been defined", (int)im->module_len, im->module,
                (int)im->name_len, im->name);
            goto fail;
        }
        const wasm_host_func *h = NULL;
        for (size_t k = 0; k < nimports && !h; k++)
            if (name_is(im->module, im->module_len, imports[k].module) && name_is(im->name, im->name_len, imports[k].name))
                h = &imports[k];
        if (!h) {
            say(err, errlen, "unknown import: `%.*s::%.*s` has not been defined", (int)im->module_len, im->module,
                (int)im->name_len, im->name);
            goto fail;
        }
        if (!sig_matches(&m->types[im->func_type], h->sig)) {
            say(err, errlen, "incompatible import type for `%.*s::%.*s`", (int)im->module_len, im->module,
                (int)im->name_len, im->name);
            goto fail;
        }
        I->hosts[f++] = h;
    }
    if (L.memories && m->nmems > L.memories) {
        say(err, errlen, "resource limit exceeded: memory count too high at %u", m->nmems);
        goto fail;
    }
    if (L.tables && m->ntables > L.tables) {
        say(err, errlen, "resource limit exceeded: table count too high at %u", m->ntables);
        goto fail;
    }
    if (m->nmems) {
        const wasm_limits *ml = &m->mems[0];
        I->has_mem = true;
        uint64_t max_pages = (ml->flags & WASM_LIMITS_HAS_MAX) ? ml->max : WASM_MAX_PAGES;
        I->mem_max = max_pages * WASM_PAGE;
        if (L.memory_bytes && L.memory_bytes < I->mem_max) I->mem_max = L.memory_bytes;
        I->mem_size = ml->min * WASM_PAGE;
        if (L.memory_bytes && I->mem_size > L.memory_bytes) {
            say(err, errlen, "memory minimum size of %llu pages exceeds memory limits", (unsigned long long)ml->min);
            goto fail;
        }
        if (!(I->mem = calloc(I->mem_size ? I->mem_size : 1, 1))) goto oom;
    }
    if (!(I->globals = calloc(m->nglobals ? m->nglobals : 1, sizeof *I->globals))) goto oom;
    for (uint32_t g = m->nglobal_imports; g < m->nglobals; g++)
        I->globals[g] = eval_const(I, &m->global_inits[g - m->nglobal_imports]);
    I->ntables = m->ntables;
    if (!(I->tables = calloc(m->ntables ? m->ntables : 1, sizeof *I->tables))) goto oom;
    for (uint32_t t = 0; t < m->ntables; t++) {
        I->tables[t].size = (uint32_t)m->tables[t].limits.min;
        I->tables[t].max = (m->tables[t].limits.flags & WASM_LIMITS_HAS_MAX) ? m->tables[t].limits.max : WASM_MAX_TABLE;
        if (!(I->tables[t].elems = calloc(I->tables[t].size ? I->tables[t].size : 1, sizeof(uint64_t)))) goto oom;
    }
    if (!(I->elem_values = calloc(m->nelems ? m->nelems : 1, sizeof *I->elem_values)) ||
        !(I->elem_len = calloc(m->nelems ? m->nelems : 1, sizeof *I->elem_len)) ||
        !(I->data_dropped = calloc(m->ndatas ? m->ndatas : 1, sizeof *I->data_dropped)))
        goto oom;
    for (uint32_t i = 0; i < m->nelems; i++) {
        const wasm_elem *e = &m->elems[i];
        if (!(I->elem_values[i] = calloc(e->count ? e->count : 1, sizeof(uint64_t)))) goto oom;
        I->elem_len[i] = e->count;
        for (uint32_t k = 0; k < e->count; k++)
            I->elem_values[i][k] = e->funcs ? (uint64_t)e->funcs[k] + 1 : eval_const(I, &e->exprs[k]);
    }
    /* Active segments, in order; then drop them (and the declarative ones). */
    for (uint32_t i = 0; i < m->nelems; i++) {
        const wasm_elem *e = &m->elems[i];
        if (e->mode == WASM_SEG_ACTIVE) {
            uint64_t at = (uint32_t)eval_const(I, &e->offset);
            table *t = &I->tables[e->table];
            if (at + e->count > t->size) {
                say(err, errlen, "%s", wasm_trap_message(WASM_TRAP_TABLE_OUT_OF_BOUNDS));
                goto fail;
            }
            if (e->count) memcpy(t->elems + at, I->elem_values[i], e->count * sizeof(uint64_t));
        }
        if (e->mode != WASM_SEG_PASSIVE) I->elem_len[i] = 0;
    }
    for (uint32_t i = 0; i < m->ndatas; i++) {
        const wasm_data *d = &m->datas[i];
        if (d->mode != WASM_SEG_ACTIVE) continue;
        uint64_t at = (uint32_t)eval_const(I, &d->offset);
        if (at + d->len > I->mem_size) {
            say(err, errlen, "%s", wasm_trap_message(WASM_TRAP_MEMORY_OUT_OF_BOUNDS));
            goto fail;
        }
        if (d->len) memcpy(I->mem + at, d->bytes, d->len);
        I->data_dropped[i] = true;
    }
    if (!(I->stack = calloc(I->stack_slots, sizeof *I->stack)) || !(I->frames = calloc(I->max_frames, sizeof *I->frames)))
        goto oom;
    if (m->has_start) {
        uint64_t io[1];
        int t = wasm_call(I, m->start, io);
        if (t != WASM_NUM_OK) {
            say(err, errlen, "wasm trap: %s (in the start function)", wasm_trap_message(t));
            goto fail;
        }
    }
    return I;
oom:
    say(err, errlen, "out of memory");
fail:
    wasm_instance_free(I);
    return NULL;
}

void *wasm_instance_user(const wasm_instance *I) { return I->user; }
const wasm_compiled *wasm_instance_compiled(const wasm_instance *I) { return I->c; }

int64_t wasm_instance_func(const wasm_instance *I, const char *name, const char *sig, char *err, size_t errlen) {
    const wasm_module *m = &I->c->m;
    const wasm_export *e = wasm_module_export(m, name, WASM_EXTERN_FUNC);
    if (!e) {
        say(err, errlen, "failed to find function export `%s`", name);
        return -1;
    }
    if (!sig_matches(wasm_func_type(m, e->index), sig)) {
        say(err, errlen, "type mismatch with parameters or results of function export `%s`", name);
        return -1;
    }
    return e->index;
}

uint8_t *wasm_memory(const wasm_instance *I, uint64_t *size) {
    if (size) *size = I->has_mem ? I->mem_size : 0;
    return I->has_mem ? I->mem : NULL;
}

void wasm_set_epoch(wasm_instance *I, const _Atomic uint64_t *epoch) { I->epoch = epoch; }
void wasm_set_deadline(wasm_instance *I, uint64_t deadline) { I->deadline = deadline; }
uint64_t wasm_deadline(const wasm_instance *I) { return I->deadline; }

/* ---- running ------------------------------------------------------------------------------ */

static int grow_memory(wasm_instance *I, uint32_t pages, uint32_t *old) {
    uint64_t before = I->mem_size / WASM_PAGE;
    *old = (uint32_t)before;
    uint64_t want = (before + pages) * WASM_PAGE;
    if (want > I->mem_max) return 0;
    if (pages == 0) return 1;
    uint8_t *p = realloc(I->mem, want);
    if (!p) return 0;
    memset(p + I->mem_size, 0, want - I->mem_size);
    I->mem = p;
    I->mem_size = want;
    return 1;
}

static int grow_table(wasm_instance *I, table *t, uint32_t n, uint64_t init, uint32_t *old) {
    *old = t->size;
    uint64_t want = (uint64_t)t->size + n;
    if (want > t->max || want > I->table_limit) return 0;
    if (n == 0) return 1;
    uint64_t *p = realloc(t->elems, want * sizeof *p);
    if (!p) return 0;
    for (uint64_t k = t->size; k < want; k++) p[k] = init;
    t->elems = p;
    t->size = (uint32_t)want;
    return 1;
}

static inline float F32(uint64_t s) { return wasm_f32_from((uint32_t)s); }
static inline double F64(uint64_t s) { return wasm_f64_from(s); }
static inline uint64_t SF32(float f) { return wasm_f32_bits(f); }
static inline uint64_t SF64(double d) { return wasm_f64_bits(d); }

#define TRAP(code)              \
    do {                        \
        trap = (code);          \
        goto trapped;           \
    } while (0)
#define CHECK_TRAP(expr)                          \
    do {                                          \
        int t_ = (expr);                          \
        if (t_ != WASM_NUM_OK) TRAP(t_);          \
    } while (0)
#define INTERRUPTED() (I->epoch && atomic_load_explicit(I->epoch, memory_order_relaxed) >= I->deadline)

/* Memory access: the effective address of an access of n bytes at the popped i32 + the offset. */
#define EA(n)                                                          \
    uint64_t ea = (uint64_t)(uint32_t)sp[-1] + pc->b;                  \
    if (ea + (n) > memsize) TRAP(WASM_TRAP_MEMORY_OUT_OF_BOUNDS)
#define LOAD(T, n, conv)                                               \
    {                                                                  \
        EA(n);                                                         \
        T v_;                                                          \
        memcpy(&v_, mem + ea, n);                                      \
        sp[-1] = conv;                                                 \
        break;                                                         \
    }
#define STORE(T, n, val)                                               \
    {                                                                  \
        uint64_t x_ = sp[-1];                                          \
        sp--;                                                          \
        EA(n);                                                         \
        T v_ = (T)(val);                                               \
        memcpy(mem + ea, &v_, n);                                      \
        sp--;                                                          \
        break;                                                         \
    }

#define BIN32(expr)                                                    \
    {                                                                  \
        uint32_t b = (uint32_t)sp[-1], a = (uint32_t)sp[-2];           \
        sp--;                                                          \
        sp[-1] = (uint32_t)(expr);                                     \
        break;                                                         \
    }
#define BIN64(expr)                                                    \
    {                                                                  \
        uint64_t b = sp[-1], a = sp[-2];                               \
        sp--;                                                          \
        sp[-1] = (uint64_t)(expr);                                     \
        break;                                                         \
    }
#define CMP32(expr) BIN32((expr) ? 1u : 0u)
#define CMP64(expr)                                                    \
    {                                                                  \
        uint64_t b = sp[-1], a = sp[-2];                               \
        sp--;                                                          \
        sp[-1] = (expr) ? 1u : 0u;                                     \
        break;                                                         \
    }
#define FBIN32(expr)                                                   \
    {                                                                  \
        float b = F32(sp[-1]), a = F32(sp[-2]);                        \
        sp--;                                                          \
        sp[-1] = SF32(expr);                                           \
        break;                                                         \
    }
#define FBIN64(expr)                                                   \
    {                                                                  \
        double b = F64(sp[-1]), a = F64(sp[-2]);                       \
        sp--;                                                          \
        sp[-1] = SF64(expr);                                           \
        break;                                                         \
    }
#define FCMP32(expr)                                                   \
    {                                                                  \
        float b = F32(sp[-1]), a = F32(sp[-2]);                        \
        sp--;                                                          \
        sp[-1] = (expr) ? 1u : 0u;                                     \
        break;                                                         \
    }
#define FCMP64(expr)                                                   \
    {                                                                  \
        double b = F64(sp[-1]), a = F64(sp[-2]);                       \
        sp--;                                                          \
        sp[-1] = (expr) ? 1u : 0u;                                     \
        break;                                                         \
    }
#define UN(expr)                                                       \
    {                                                                  \
        uint64_t a = sp[-1];                                           \
        sp[-1] = (expr);                                               \
        break;                                                         \
    }
#define TRUNC(fn, conv, T)                                             \
    {                                                                  \
        T r_ = 0;                                                      \
        CHECK_TRAP(fn(conv, &r_));                                     \
        sp[-1] = r_;                                                   \
        break;                                                         \
    }

/* Moves `values` slots from the top of the stack to fp[height..], and the stack's top after them. */
#define BRANCH(height, values)                                         \
    do {                                                               \
        uint32_t n_ = (values);                                        \
        uint64_t *dst_ = fp + (height);                                \
        if (n_ && dst_ != sp - n_) memmove(dst_, sp - n_, n_ * 8);     \
        sp = dst_ + n_;                                                \
    } while (0)

int wasm_call(wasm_instance *I, uint32_t func, uint64_t *io) {
    const wasm_compiled *C = I->c;
    const wasm_module *M = &C->m;
    uint32_t nimp = M->nfunc_imports;
    if (func < nimp) {
        const wasm_host_func *h = I->hosts[func];
        return h->fn(I, h->ctx, io);
    }
    uint64_t *stack_end = I->stack + I->stack_slots;
    const wasm_fn_code *fn = &C->fns[func - nimp];
    uint64_t *fp = I->stack;
    if (fp + fn->frame_slots > stack_end) return WASM_TRAP_STACK_OVERFLOW;
    if (fn->nparams) memcpy(fp, io, fn->nparams * 8);
    memset(fp + fn->nparams, 0, (fn->nlocals - fn->nparams) * 8);
    if (INTERRUPTED()) return WASM_TRAP_INTERRUPT;
    uint64_t *sp = fp + fn->nlocals;
    const wasm_ins *pc = fn->code;
    uint32_t depth = 0;
    uint8_t *mem = I->mem;
    uint64_t memsize = I->mem_size;
    int trap = WASM_NUM_OK;
    uint32_t target;

    for (;;) {
        switch (pc->op) {
        case 0x00: TRAP(WASM_TRAP_UNREACHABLE);
        case WASM_INS_INTERRUPT:
            if (INTERRUPTED()) TRAP(WASM_TRAP_INTERRUPT);
            break;
        case WASM_INS_JUMP: pc = fn->code + pc->a; continue;
        case WASM_INS_BR:
            BRANCH(pc->b >> 32, (uint32_t)pc->b);
            pc = fn->code + pc->a;
            continue;
        case WASM_INS_BR_IF:
            if ((uint32_t)*--sp) {
                BRANCH(pc->b >> 32, (uint32_t)pc->b);
                pc = fn->code + pc->a;
                continue;
            }
            break;
        case WASM_INS_BR_UNLESS:
            if (!(uint32_t)*--sp) {
                pc = fn->code + pc->a;
                continue;
            }
            break;
        case WASM_INS_BR_TABLE: {
            uint32_t i = (uint32_t)*--sp;
            if (i >= pc->b - 1) i = (uint32_t)pc->b - 1;
            const wasm_br_entry *e = &fn->brs[pc->a + i];
            BRANCH(e->height, e->values);
            pc = fn->code + e->target;
            continue;
        }
        case WASM_INS_RETURN: {
            uint32_t n = pc->a;
            if (n && fp != sp - n) memmove(fp, sp - n, n * 8);
            if (depth == 0) {
                if (n) memcpy(io, fp, n * 8);
                return WASM_NUM_OK;
            }
            sp = fp + n;
            frame *f = &I->frames[--depth];
            fn = f->fn;
            pc = f->pc;
            fp = f->fp;
            continue;
        }
        case WASM_INS_CALL_INDIRECT: {
            table *t = &I->tables[pc->b];
            uint32_t i = (uint32_t)*--sp;
            if (i >= t->size) TRAP(WASM_TRAP_TABLE_OUT_OF_BOUNDS);
            uint64_t ref = t->elems[i];
            if (!ref) TRAP(WASM_TRAP_INDIRECT_CALL_TO_NULL);
            target = (uint32_t)(ref - 1);
            if (C->canon[M->funcs[target]] != C->canon[pc->a]) TRAP(WASM_TRAP_BAD_SIGNATURE);
            goto call;
        }
        case WASM_INS_CALL:
            target = pc->a;
        call:
            if (target < nimp) {
                const wasm_host_func *h = I->hosts[target];
                const wasm_functype *ft = &M->types[M->funcs[target]];
                uint64_t *args = sp - ft->nparams;
                /* Room for the results where the arguments were. */
                if (args + ft->nresults > stack_end) TRAP(WASM_TRAP_STACK_OVERFLOW);
                CHECK_TRAP(h->fn(I, h->ctx, args));
                sp = args + ft->nresults;
                mem = I->mem;
                memsize = I->mem_size;
                break;
            } else {
                const wasm_fn_code *callee = &C->fns[target - nimp];
                uint64_t *nfp = sp - callee->nparams;
                if (depth == I->max_frames || nfp + callee->frame_slots > stack_end) TRAP(WASM_TRAP_STACK_OVERFLOW);
                I->frames[depth++] = (frame){fn, pc + 1, fp};
                fn = callee;
                fp = nfp;
                memset(fp + fn->nparams, 0, (fn->nlocals - fn->nparams) * 8);
                sp = fp + fn->nlocals;
                pc = fn->code;
                if (INTERRUPTED()) TRAP(WASM_TRAP_INTERRUPT);
                continue;
            }
        case WASM_OP_DROP: sp--; break;
        case WASM_OP_SELECT: {
            uint32_t c = (uint32_t)sp[-1];
            sp -= 2;
            if (!c) sp[-1] = sp[0];
            break;
        }
        case WASM_OP_LOCAL_GET: *sp++ = fp[pc->a]; break;
        case WASM_OP_LOCAL_SET: fp[pc->a] = *--sp; break;
        case WASM_OP_LOCAL_TEE: fp[pc->a] = sp[-1]; break;
        case WASM_OP_GLOBAL_GET: *sp++ = I->globals[pc->a]; break;
        case WASM_OP_GLOBAL_SET: I->globals[pc->a] = *--sp; break;
        case WASM_OP_TABLE_GET: {
            table *t = &I->tables[pc->a];
            uint32_t i = (uint32_t)sp[-1];
            if (i >= t->size) TRAP(WASM_TRAP_TABLE_OUT_OF_BOUNDS);
            sp[-1] = t->elems[i];
            break;
        }
        case WASM_OP_TABLE_SET: {
            table *t = &I->tables[pc->a];
            uint32_t i = (uint32_t)sp[-2];
            if (i >= t->size) TRAP(WASM_TRAP_TABLE_OUT_OF_BOUNDS);
            t->elems[i] = sp[-1];
            sp -= 2;
            break;
        }
        case 0x28: LOAD(uint32_t, 4, v_)
        case 0x29: LOAD(uint64_t, 8, v_)
        case 0x2a: LOAD(uint32_t, 4, v_)
        case 0x2b: LOAD(uint64_t, 8, v_)
        case 0x2c: LOAD(int8_t, 1, (uint32_t)(int32_t)v_)
        case 0x2d: LOAD(uint8_t, 1, (uint32_t)v_)
        case 0x2e: LOAD(int16_t, 2, (uint32_t)(int32_t)v_)
        case 0x2f: LOAD(uint16_t, 2, (uint32_t)v_)
        case 0x30: LOAD(int8_t, 1, (uint64_t)(int64_t)v_)
        case 0x31: LOAD(uint8_t, 1, (uint64_t)v_)
        case 0x32: LOAD(int16_t, 2, (uint64_t)(int64_t)v_)
        case 0x33: LOAD(uint16_t, 2, (uint64_t)v_)
        case 0x34: LOAD(int32_t, 4, (uint64_t)(int64_t)v_)
        case 0x35: LOAD(uint32_t, 4, (uint64_t)v_)
        case 0x36: STORE(uint32_t, 4, x_)
        case 0x37: STORE(uint64_t, 8, x_)
        case 0x38: STORE(uint32_t, 4, x_)
        case 0x39: STORE(uint64_t, 8, x_)
        case 0x3a: STORE(uint8_t, 1, x_)
        case 0x3b: STORE(uint16_t, 2, x_)
        case 0x3c: STORE(uint8_t, 1, x_)
        case 0x3d: STORE(uint16_t, 2, x_)
        case 0x3e: STORE(uint32_t, 4, x_)
        case WASM_OP_MEMORY_SIZE: *sp++ = (uint32_t)(memsize / WASM_PAGE); break;
        case WASM_OP_MEMORY_GROW: {
            uint32_t old;
            sp[-1] = grow_memory(I, (uint32_t)sp[-1], &old) ? old : 0xffffffffu;
            mem = I->mem;
            memsize = I->mem_size;
            break;
        }
        case WASM_OP_I32_CONST:
        case WASM_OP_I64_CONST:
        case WASM_OP_F32_CONST:
        case WASM_OP_F64_CONST: *sp++ = pc->b; break;
        case 0x45: UN((uint32_t)a == 0)
        case 0x46: CMP32(a == b)
        case 0x47: CMP32(a != b)
        case 0x48: CMP32((int32_t)a < (int32_t)b)
        case 0x49: CMP32(a < b)
        case 0x4a: CMP32((int32_t)a > (int32_t)b)
        case 0x4b: CMP32(a > b)
        case 0x4c: CMP32((int32_t)a <= (int32_t)b)
        case 0x4d: CMP32(a <= b)
        case 0x4e: CMP32((int32_t)a >= (int32_t)b)
        case 0x4f: CMP32(a >= b)
        case 0x50: UN(a == 0)
        case 0x51: CMP64(a == b)
        case 0x52: CMP64(a != b)
        case 0x53: CMP64((int64_t)a < (int64_t)b)
        case 0x54: CMP64(a < b)
        case 0x55: CMP64((int64_t)a > (int64_t)b)
        case 0x56: CMP64(a > b)
        case 0x57: CMP64((int64_t)a <= (int64_t)b)
        case 0x58: CMP64(a <= b)
        case 0x59: CMP64((int64_t)a >= (int64_t)b)
        case 0x5a: CMP64(a >= b)
        case 0x5b: FCMP32(a == b)
        case 0x5c: FCMP32(a != b)
        case 0x5d: FCMP32(a < b)
        case 0x5e: FCMP32(a > b)
        case 0x5f: FCMP32(a <= b)
        case 0x60: FCMP32(a >= b)
        case 0x61: FCMP64(a == b)
        case 0x62: FCMP64(a != b)
        case 0x63: FCMP64(a < b)
        case 0x64: FCMP64(a > b)
        case 0x65: FCMP64(a <= b)
        case 0x66: FCMP64(a >= b)
        case 0x67: UN(wasm_i32_clz((uint32_t)a))
        case 0x68: UN(wasm_i32_ctz((uint32_t)a))
        case 0x69: UN(wasm_i32_popcnt((uint32_t)a))
        case 0x6a: BIN32(a + b)
        case 0x6b: BIN32(a - b)
        case 0x6c: BIN32(a * b)
        case 0x6d: {
            uint32_t r;
            CHECK_TRAP(wasm_i32_div_s((uint32_t)sp[-2], (uint32_t)sp[-1], &r));
            sp--;
            sp[-1] = r;
            break;
        }
        case 0x6e: {
            uint32_t r;
            CHECK_TRAP(wasm_i32_div_u((uint32_t)sp[-2], (uint32_t)sp[-1], &r));
            sp--;
            sp[-1] = r;
            break;
        }
        case 0x6f: {
            uint32_t r;
            CHECK_TRAP(wasm_i32_rem_s((uint32_t)sp[-2], (uint32_t)sp[-1], &r));
            sp--;
            sp[-1] = r;
            break;
        }
        case 0x70: {
            uint32_t r;
            CHECK_TRAP(wasm_i32_rem_u((uint32_t)sp[-2], (uint32_t)sp[-1], &r));
            sp--;
            sp[-1] = r;
            break;
        }
        case 0x71: BIN32(a & b)
        case 0x72: BIN32(a | b)
        case 0x73: BIN32(a ^ b)
        case 0x74: BIN32(a << (b & 31))
        case 0x75: BIN32(wasm_i32_shr_s(a, b))
        case 0x76: BIN32(a >> (b & 31))
        case 0x77: BIN32(wasm_i32_rotl(a, b))
        case 0x78: BIN32(wasm_i32_rotr(a, b))
        case 0x79: UN(wasm_i64_clz(a))
        case 0x7a: UN(wasm_i64_ctz(a))
        case 0x7b: UN(wasm_i64_popcnt(a))
        case 0x7c: BIN64(a + b)
        case 0x7d: BIN64(a - b)
        case 0x7e: BIN64(a * b)
        case 0x7f: {
            uint64_t r;
            CHECK_TRAP(wasm_i64_div_s(sp[-2], sp[-1], &r));
            sp--;
            sp[-1] = r;
            break;
        }
        case 0x80: {
            uint64_t r;
            CHECK_TRAP(wasm_i64_div_u(sp[-2], sp[-1], &r));
            sp--;
            sp[-1] = r;
            break;
        }
        case 0x81: {
            uint64_t r;
            CHECK_TRAP(wasm_i64_rem_s(sp[-2], sp[-1], &r));
            sp--;
            sp[-1] = r;
            break;
        }
        case 0x82: {
            uint64_t r;
            CHECK_TRAP(wasm_i64_rem_u(sp[-2], sp[-1], &r));
            sp--;
            sp[-1] = r;
            break;
        }
        case 0x83: BIN64(a & b)
        case 0x84: BIN64(a | b)
        case 0x85: BIN64(a ^ b)
        case 0x86: BIN64(a << (b & 63))
        case 0x87: BIN64(wasm_i64_shr_s(a, b))
        case 0x88: BIN64(a >> (b & 63))
        case 0x89: BIN64(wasm_i64_rotl(a, b))
        case 0x8a: BIN64(wasm_i64_rotr(a, b))
        case 0x8b: UN(wasm_f32_abs((uint32_t)a))
        case 0x8c: UN(wasm_f32_neg((uint32_t)a))
        case 0x8d: UN(SF32(wasm_f32_ceil(F32(a))))
        case 0x8e: UN(SF32(wasm_f32_floor(F32(a))))
        case 0x8f: UN(SF32(wasm_f32_trunc(F32(a))))
        case 0x90: UN(SF32(wasm_f32_nearest(F32(a))))
        case 0x91: UN(SF32(wasm_f32_sqrt(F32(a))))
        case 0x92: FBIN32(wasm_f32_add(a, b))
        case 0x93: FBIN32(wasm_f32_sub(a, b))
        case 0x94: FBIN32(wasm_f32_mul(a, b))
        case 0x95: FBIN32(wasm_f32_div(a, b))
        case 0x96: FBIN32(wasm_f32_min(a, b))
        case 0x97: FBIN32(wasm_f32_max(a, b))
        case 0x98: BIN32(wasm_f32_copysign(a, b))
        case 0x99: UN(wasm_f64_abs(a))
        case 0x9a: UN(wasm_f64_neg(a))
        case 0x9b: UN(SF64(wasm_f64_ceil(F64(a))))
        case 0x9c: UN(SF64(wasm_f64_floor(F64(a))))
        case 0x9d: UN(SF64(wasm_f64_trunc(F64(a))))
        case 0x9e: UN(SF64(wasm_f64_nearest(F64(a))))
        case 0x9f: UN(SF64(wasm_f64_sqrt(F64(a))))
        case 0xa0: FBIN64(wasm_f64_add(a, b))
        case 0xa1: FBIN64(wasm_f64_sub(a, b))
        case 0xa2: FBIN64(wasm_f64_mul(a, b))
        case 0xa3: FBIN64(wasm_f64_div(a, b))
        case 0xa4: FBIN64(wasm_f64_min(a, b))
        case 0xa5: FBIN64(wasm_f64_max(a, b))
        case 0xa6: BIN64(wasm_f64_copysign(a, b))
        case 0xa7: UN((uint32_t)a)
        case 0xa8: TRUNC(wasm_i32_trunc_f32_s, F32(sp[-1]), uint32_t)
        case 0xa9: TRUNC(wasm_i32_trunc_f32_u, F32(sp[-1]), uint32_t)
        case 0xaa: TRUNC(wasm_i32_trunc_f64_s, F64(sp[-1]), uint32_t)
        case 0xab: TRUNC(wasm_i32_trunc_f64_u, F64(sp[-1]), uint32_t)
        case 0xac: UN((uint64_t)(int64_t)(int32_t)(uint32_t)a)
        case 0xad: UN((uint32_t)a)
        case 0xae: TRUNC(wasm_i64_trunc_f32_s, F32(sp[-1]), uint64_t)
        case 0xaf: TRUNC(wasm_i64_trunc_f32_u, F32(sp[-1]), uint64_t)
        case 0xb0: TRUNC(wasm_i64_trunc_f64_s, F64(sp[-1]), uint64_t)
        case 0xb1: TRUNC(wasm_i64_trunc_f64_u, F64(sp[-1]), uint64_t)
        case 0xb2: UN(SF32((float)(int32_t)(uint32_t)a))
        case 0xb3: UN(SF32((float)(uint32_t)a))
        case 0xb4: UN(SF32((float)(int64_t)a))
        case 0xb5: UN(SF32((float)a))
        case 0xb6: UN(SF32(wasm_f32_demote(F64(a))))
        case 0xb7: UN(SF64((double)(int32_t)(uint32_t)a))
        case 0xb8: UN(SF64((double)(uint32_t)a))
        case 0xb9: UN(SF64((double)(int64_t)a))
        case 0xba: UN(SF64((double)a))
        case 0xbb: UN(SF64(wasm_f64_promote(F32(a))))
        case 0xbc: /* reinterprets: the slot holds the bits already */
        case 0xbd:
        case 0xbe:
        case 0xbf: break;
        case 0xc0: UN((uint32_t)(int32_t)(int8_t)(uint8_t)a)
        case 0xc1: UN((uint32_t)(int32_t)(int16_t)(uint16_t)a)
        case 0xc2: UN((uint64_t)(int64_t)(int8_t)(uint8_t)a)
        case 0xc3: UN((uint64_t)(int64_t)(int16_t)(uint16_t)a)
        case 0xc4: UN((uint64_t)(int64_t)(int32_t)(uint32_t)a)
        case WASM_OP_REF_NULL: *sp++ = 0; break;
        case WASM_OP_REF_IS_NULL: sp[-1] = sp[-1] == 0; break;
        case WASM_OP_REF_FUNC: *sp++ = (uint64_t)pc->a + 1; break;
        case WASM_INS_FC + 0: UN(wasm_i32_trunc_sat_f32_s(F32(a)))
        case WASM_INS_FC + 1: UN(wasm_i32_trunc_sat_f32_u(F32(a)))
        case WASM_INS_FC + 2: UN(wasm_i32_trunc_sat_f64_s(F64(a)))
        case WASM_INS_FC + 3: UN(wasm_i32_trunc_sat_f64_u(F64(a)))
        case WASM_INS_FC + 4: UN(wasm_i64_trunc_sat_f32_s(F32(a)))
        case WASM_INS_FC + 5: UN(wasm_i64_trunc_sat_f32_u(F32(a)))
        case WASM_INS_FC + 6: UN(wasm_i64_trunc_sat_f64_s(F64(a)))
        case WASM_INS_FC + 7: UN(wasm_i64_trunc_sat_f64_u(F64(a)))
        case WASM_INS_FC + 8: { /* memory.init */
            uint64_t n = (uint32_t)sp[-1], s = (uint32_t)sp[-2], d = (uint32_t)sp[-3];
            sp -= 3;
            const wasm_data *seg = &M->datas[pc->a];
            uint64_t len = I->data_dropped[pc->a] ? 0 : seg->len;
            if (s + n > len || d + n > memsize) TRAP(WASM_TRAP_MEMORY_OUT_OF_BOUNDS);
            if (n) memcpy(mem + d, seg->bytes + s, n);
            break;
        }
        case WASM_INS_FC + 9: I->data_dropped[pc->a] = true; break;
        case WASM_INS_FC + 10: { /* memory.copy */
            uint64_t n = (uint32_t)sp[-1], s = (uint32_t)sp[-2], d = (uint32_t)sp[-3];
            sp -= 3;
            if (s + n > memsize || d + n > memsize) TRAP(WASM_TRAP_MEMORY_OUT_OF_BOUNDS);
            if (n) memmove(mem + d, mem + s, n);
            break;
        }
        case WASM_INS_FC + 11: { /* memory.fill */
            uint64_t n = (uint32_t)sp[-1], d = (uint32_t)sp[-3];
            uint8_t v = (uint8_t)sp[-2];
            sp -= 3;
            if (d + n > memsize) TRAP(WASM_TRAP_MEMORY_OUT_OF_BOUNDS);
            if (n) memset(mem + d, v, n);
            break;
        }
        case WASM_INS_FC + 12: { /* table.init elem a, table b */
            uint64_t n = (uint32_t)sp[-1], s = (uint32_t)sp[-2], d = (uint32_t)sp[-3];
            sp -= 3;
            table *t = &I->tables[pc->b];
            if (s + n > I->elem_len[pc->a] || d + n > t->size) TRAP(WASM_TRAP_TABLE_OUT_OF_BOUNDS);
            if (n) memcpy(t->elems + d, I->elem_values[pc->a] + s, n * sizeof(uint64_t));
            break;
        }
        case WASM_INS_FC + 13: I->elem_len[pc->a] = 0; break;
        case WASM_INS_FC + 14: { /* table.copy dst a, src b */
            uint64_t n = (uint32_t)sp[-1], s = (uint32_t)sp[-2], d = (uint32_t)sp[-3];
            sp -= 3;
            table *td = &I->tables[pc->a], *ts = &I->tables[pc->b];
            if (s + n > ts->size || d + n > td->size) TRAP(WASM_TRAP_TABLE_OUT_OF_BOUNDS);
            if (n) memmove(td->elems + d, ts->elems + s, n * sizeof(uint64_t));
            break;
        }
        case WASM_INS_FC + 15: { /* table.grow */
            uint32_t old;
            uint32_t n = (uint32_t)sp[-1];
            uint64_t init = sp[-2];
            sp--;
            sp[-1] = grow_table(I, &I->tables[pc->a], n, init, &old) ? old : 0xffffffffu;
            break;
        }
        case WASM_INS_FC + 16: *sp++ = I->tables[pc->a].size; break;
        case WASM_INS_FC + 17: { /* table.fill */
            uint64_t n = (uint32_t)sp[-1], d = (uint32_t)sp[-3];
            uint64_t v = sp[-2];
            sp -= 3;
            table *t = &I->tables[pc->a];
            if (d + n > t->size) TRAP(WASM_TRAP_TABLE_OUT_OF_BOUNDS);
            for (uint64_t k = 0; k < n; k++) t->elems[d + k] = v;
            break;
        }
        default: TRAP(WASM_TRAP_UNREACHABLE); /* lowering emits nothing else */
        }
        pc++;
    }
trapped:
    return trap;
}
