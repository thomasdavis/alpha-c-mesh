/* One pass over a validated body: instructions out, branches patched as their targets appear. */
#include "wasm/lower.h"

#include "wasm/instr.h"
#include "wasm/reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A patch names an instruction's `a` or, with this bit, a br-table entry's target. */
#define PATCH_ENTRY 0x80000000u

typedef struct {
    uint32_t op;       /* block, loop, if, or 0: the function */
    uint32_t height;   /* operand height where the block's values start */
    uint32_t nparams, nresults;
    uint32_t start;    /* loop: its label's target */
    uint32_t *patches;
    uint32_t npatches, cap;
    int64_t else_patch; /* the if's BR_UNLESS, or -1 */
    bool unreachable;  /* the rest of this frame's current branch is dead */
    bool dead;         /* entered from dead code: nothing in it is emitted */
} lframe;

typedef struct {
    const wasm_module *m;
    wasm_fn_code *out;
    uint32_t cap, brcap;
    lframe *ctl;
    uint32_t nctl, ccap;
    uint32_t h, max_h;
    bool oom;
} L;

static uint32_t emit(L *l, uint16_t op, uint32_t a, uint64_t b) {
    wasm_fn_code *c = l->out;
    if (c->ncode == l->cap) {
        uint32_t cap = l->cap ? l->cap * 2 : 64;
        wasm_ins *p = realloc(c->code, cap * sizeof *p);
        if (!p) {
            l->oom = true;
            return 0;
        }
        c->code = p;
        l->cap = cap;
    }
    c->code[c->ncode] = (wasm_ins){op, 0, a, b};
    return c->ncode++;
}

static void patch_later(L *l, lframe *f, uint32_t p) {
    if (f->npatches == f->cap) {
        uint32_t cap = f->cap ? f->cap * 2 : 4;
        uint32_t *q = realloc(f->patches, cap * sizeof *q);
        if (!q) {
            l->oom = true;
            return;
        }
        f->patches = q;
        f->cap = cap;
    }
    f->patches[f->npatches++] = p;
}

static void set_h(L *l, uint32_t h) {
    l->h = h;
    if (h > l->max_h) l->max_h = h;
}

static void push_frame(L *l, uint32_t op, uint32_t nparams, uint32_t nresults, bool dead) {
    if (l->nctl == l->ccap) {
        uint32_t cap = l->ccap ? l->ccap * 2 : 16;
        lframe *p = realloc(l->ctl, cap * sizeof *p);
        if (!p) {
            l->oom = true;
            return;
        }
        l->ctl = p;
        l->ccap = cap;
    }
    lframe *f = &l->ctl[l->nctl++];
    memset(f, 0, sizeof *f);
    f->op = op;
    f->nparams = nparams;
    f->nresults = nresults;
    f->height = dead ? 0 : l->h - nparams;
    f->else_patch = -1;
    f->dead = dead;
}

static void block_arity(const wasm_module *m, const wasm_instr *in, uint32_t *np, uint32_t *nr) {
    if (in->blocktype >= 0) {
        *np = m->types[in->blocktype].nparams;
        *nr = m->types[in->blocktype].nresults;
    } else {
        *np = 0;
        *nr = in->blocktype == WASM_BT_EMPTY ? 0 : 1;
    }
}

/* A branch to the frame `depth` levels out: its target (now, or patched at its end), the height
 * its values move to, how many. */
static void branch(L *l, uint16_t op, uint32_t depth) {
    lframe *f = &l->ctl[l->nctl - 1 - depth];
    bool loop = f->op == WASM_OP_LOOP;
    uint32_t values = loop ? f->nparams : f->nresults;
    uint64_t b = (uint64_t)(l->out->nlocals + f->height) << 32 | values;
    uint32_t i = emit(l, op, loop ? f->start : 0, b);
    if (!loop) patch_later(l, f, i);
}

static void br_table(L *l, const wasm_instr *in) {
    wasm_fn_code *c = l->out;
    uint32_t n = in->ntargets + 1;
    uint32_t *t = malloc(n * sizeof *t);
    if (!t || c->nbrs + n < c->nbrs) {
        free(t);
        l->oom = true;
        return;
    }
    wasm_instr_targets(in, t);
    if (c->nbrs + n > l->brcap) {
        uint32_t cap = l->brcap ? l->brcap : 16;
        while (cap < c->nbrs + n) cap *= 2;
        wasm_br_entry *p = realloc(c->brs, cap * sizeof *p);
        if (!p) {
            free(t);
            l->oom = true;
            return;
        }
        c->brs = p;
        l->brcap = cap;
    }
    uint32_t first = c->nbrs;
    for (uint32_t k = 0; k < n; k++) {
        lframe *f = &l->ctl[l->nctl - 1 - t[k]];
        bool loop = f->op == WASM_OP_LOOP;
        c->brs[first + k] = (wasm_br_entry){loop ? f->start : 0, c->nlocals + f->height, loop ? f->nparams : f->nresults};
        if (!loop) patch_later(l, f, PATCH_ENTRY | (first + k));
    }
    c->nbrs += n;
    free(t);
    emit(l, WASM_INS_BR_TABLE, first, n);
}

static void resolve(L *l, lframe *f, uint32_t target) {
    for (uint32_t k = 0; k < f->npatches; k++) {
        uint32_t p = f->patches[k];
        if (p & PATCH_ENTRY) l->out->brs[p & ~PATCH_ENTRY].target = target;
        else l->out->code[p].a = target;
    }
    f->npatches = 0;
}

/* Pops and pushes of a plain instruction, from its signature. */
static void sig_effect(const char *sig, uint32_t *pops, uint32_t *pushes) {
    const char *gt = strchr(sig, '>');
    *pops = (uint32_t)(gt - sig);
    *pushes = (uint32_t)strlen(gt + 1);
}

static bool instr(L *l, const wasm_instr *in) {
    const wasm_module *m = l->m;
    lframe *f = &l->ctl[l->nctl - 1];
    uint32_t np, nr;
    /* Dead code: only the block structure is followed. */
    if (f->dead || f->unreachable) {
        switch (in->op) {
        case WASM_OP_BLOCK:
        case WASM_OP_LOOP:
        case WASM_OP_IF: push_frame(l, in->op, 0, 0, true); return true;
        case WASM_OP_ELSE:
        case WASM_OP_END:
            if (f->dead) {
                if (in->op == WASM_OP_END) l->nctl--;
                return true;
            }
            break; /* a live frame's dead branch ends: handled below */
        default: return true;
        }
    }
    if (in->info.sig && in->op != WASM_OP_MEMORY_SIZE && in->op != WASM_OP_MEMORY_GROW) {
        uint32_t pops, pushes;
        sig_effect(in->info.sig, &pops, &pushes);
        uint64_t b = 0;
        switch (in->info.imm) {
        case WASM_IMM_MEMARG: b = in->moffset; break;
        case WASM_IMM_I32: b = (uint32_t)in->i; break;
        case WASM_IMM_I64: b = (uint64_t)in->i; break;
        case WASM_IMM_F32: b = in->f32; break;
        case WASM_IMM_F64: b = in->f64; break;
        default: break;
        }
        uint16_t op = in->op > 0xff ? (uint16_t)(WASM_INS_FC + (in->op & 0xffff)) : (uint16_t)in->op;
        emit(l, op, 0, b);
        set_h(l, l->h - pops + pushes);
        return true;
    }
    switch (in->op) {
    case WASM_OP_UNREACHABLE:
        emit(l, 0x00, 0, 0);
        f->unreachable = true;
        return true;
    case WASM_OP_NOP: return true;
    case WASM_OP_BLOCK:
        block_arity(m, in, &np, &nr);
        push_frame(l, WASM_OP_BLOCK, np, nr, false);
        return true;
    case WASM_OP_LOOP:
        block_arity(m, in, &np, &nr);
        push_frame(l, WASM_OP_LOOP, np, nr, false);
        if (!l->oom) l->ctl[l->nctl - 1].start = emit(l, WASM_INS_INTERRUPT, 0, 0);
        return true;
    case WASM_OP_IF: {
        block_arity(m, in, &np, &nr);
        set_h(l, l->h - 1);
        uint32_t i = emit(l, WASM_INS_BR_UNLESS, 0, 0);
        push_frame(l, WASM_OP_IF, np, nr, false);
        if (!l->oom) l->ctl[l->nctl - 1].else_patch = i;
        return true;
    }
    case WASM_OP_ELSE:
        if (!f->unreachable) patch_later(l, f, emit(l, WASM_INS_JUMP, 0, 0));
        if (f->else_patch >= 0) l->out->code[f->else_patch].a = l->out->ncode;
        f->else_patch = -1;
        f->unreachable = false;
        set_h(l, f->height + f->nparams);
        return true;
    case WASM_OP_END: {
        uint32_t here = l->out->ncode;
        if (f->else_patch >= 0) l->out->code[f->else_patch].a = here;
        resolve(l, f, here);
        if (f->op == 0) emit(l, WASM_INS_RETURN, f->nresults, 0);
        set_h(l, f->height + f->nresults);
        free(f->patches);
        l->nctl--;
        return true;
    }
    case WASM_OP_BR:
        branch(l, WASM_INS_BR, in->a);
        f->unreachable = true;
        return true;
    case WASM_OP_BR_IF:
        set_h(l, l->h - 1);
        branch(l, WASM_INS_BR_IF, in->a);
        return true;
    case WASM_OP_BR_TABLE:
        set_h(l, l->h - 1);
        br_table(l, in);
        f->unreachable = true;
        return true;
    case WASM_OP_RETURN:
        emit(l, WASM_INS_RETURN, l->out->nresults, 0);
        f->unreachable = true;
        return true;
    case WASM_OP_CALL: {
        const wasm_functype *t = wasm_func_type(m, in->a);
        emit(l, WASM_INS_CALL, in->a, 0);
        set_h(l, l->h - t->nparams + t->nresults);
        return true;
    }
    case WASM_OP_CALL_INDIRECT: {
        const wasm_functype *t = &m->types[in->a];
        emit(l, WASM_INS_CALL_INDIRECT, in->a, in->b);
        set_h(l, l->h - 1 - t->nparams + t->nresults);
        return true;
    }
    case WASM_OP_DROP:
        emit(l, WASM_OP_DROP, 0, 0);
        set_h(l, l->h - 1);
        return true;
    case WASM_OP_SELECT:
    case WASM_OP_SELECT_T:
        emit(l, WASM_OP_SELECT, 0, 0);
        set_h(l, l->h - 2);
        return true;
    case WASM_OP_LOCAL_GET:
        emit(l, WASM_OP_LOCAL_GET, in->a, 0);
        set_h(l, l->h + 1);
        return true;
    case WASM_OP_LOCAL_SET:
        emit(l, WASM_OP_LOCAL_SET, in->a, 0);
        set_h(l, l->h - 1);
        return true;
    case WASM_OP_LOCAL_TEE: emit(l, WASM_OP_LOCAL_TEE, in->a, 0); return true;
    case WASM_OP_GLOBAL_GET:
        emit(l, WASM_OP_GLOBAL_GET, in->a, 0);
        set_h(l, l->h + 1);
        return true;
    case WASM_OP_GLOBAL_SET:
        emit(l, WASM_OP_GLOBAL_SET, in->a, 0);
        set_h(l, l->h - 1);
        return true;
    case WASM_OP_TABLE_GET: emit(l, WASM_OP_TABLE_GET, in->a, 0); return true;
    case WASM_OP_TABLE_SET:
        emit(l, WASM_OP_TABLE_SET, in->a, 0);
        set_h(l, l->h - 2);
        return true;
    case WASM_OP_MEMORY_SIZE:
        emit(l, WASM_OP_MEMORY_SIZE, 0, 0);
        set_h(l, l->h + 1);
        return true;
    case WASM_OP_MEMORY_GROW: emit(l, WASM_OP_MEMORY_GROW, 0, 0); return true;
    case WASM_OP_REF_NULL:
        emit(l, WASM_OP_REF_NULL, 0, 0);
        set_h(l, l->h + 1);
        return true;
    case WASM_OP_REF_IS_NULL: emit(l, WASM_OP_REF_IS_NULL, 0, 0); return true;
    case WASM_OP_REF_FUNC:
        emit(l, WASM_OP_REF_FUNC, in->a, 0);
        set_h(l, l->h + 1);
        return true;
    case WASM_OP_MEMORY_INIT:
        emit(l, WASM_INS_FC + 8, in->a, 0);
        set_h(l, l->h - 3);
        return true;
    case WASM_OP_DATA_DROP: emit(l, WASM_INS_FC + 9, in->a, 0); return true;
    case WASM_OP_MEMORY_COPY:
    case WASM_OP_MEMORY_FILL:
        emit(l, (uint16_t)(WASM_INS_FC + (in->op & 0xffff)), 0, 0);
        set_h(l, l->h - 3);
        return true;
    case WASM_OP_TABLE_INIT:
    case WASM_OP_TABLE_COPY:
        emit(l, (uint16_t)(WASM_INS_FC + (in->op & 0xffff)), in->a, in->b);
        set_h(l, l->h - 3);
        return true;
    case WASM_OP_ELEM_DROP: emit(l, WASM_INS_FC + 13, in->a, 0); return true;
    case WASM_OP_TABLE_GROW:
        emit(l, WASM_INS_FC + 15, in->a, 0);
        set_h(l, l->h - 1);
        return true;
    case WASM_OP_TABLE_SIZE:
        emit(l, WASM_INS_FC + 16, in->a, 0);
        set_h(l, l->h + 1);
        return true;
    case WASM_OP_TABLE_FILL:
        emit(l, WASM_INS_FC + 17, in->a, 0);
        set_h(l, l->h - 3);
        return true;
    default: return false;
    }
}

bool wasm_lower(const wasm_module *m, uint32_t func, wasm_fn_code *out, char *err, size_t errlen) {
    memset(out, 0, sizeof *out);
    if (err && errlen) err[0] = 0;
    const wasm_code *code = &m->codes[func - m->nfunc_imports];
    const wasm_functype *t = wasm_func_type(m, func);
    out->nparams = t->nparams;
    out->nresults = t->nresults;
    out->nlocals = (uint32_t)(t->nparams + code->nlocals);
    L l = {0};
    l.m = m;
    l.out = out;
    wasm_reader r = wasm_reader_at(m->bytes, code->body.start, code->body.start + code->body.len, err, errlen);
    push_frame(&l, 0, 0, t->nresults, false);
    wasm_instr in;
    bool ok = true;
    while (ok && l.nctl > 0 && !l.oom) {
        if (!wasm_rd_instr(&r, &in)) {
            ok = false;
            break;
        }
        if (in.info.feature & (WASM_F_SIMD | WASM_F_RELAXED_SIMD | WASM_F_THREADS)) {
            ok = wasm_rd_fail_at(&r, in.offset, "%s is not supported by this executor (alpha-c-mesh's wasm interpreter)",
                                 in.info.feature == WASM_F_THREADS ? "threads" : "SIMD");
            break;
        }
        if (!instr(&l, &in)) ok = wasm_rd_fail_at(&r, in.offset, "%s is not supported by this executor", in.info.name);
    }
    if (l.oom && ok) ok = wasm_rd_fail_at(&r, code->offset, "out of memory");
    for (uint32_t k = 0; k < l.nctl; k++) free(l.ctl[k].patches);
    free(l.ctl);
    out->frame_slots = out->nlocals + l.max_h + 1;
    if (!ok) wasm_fn_code_free(out);
    return ok;
}

void wasm_fn_code_free(wasm_fn_code *c) {
    free(c->code);
    free(c->brs);
    memset(c, 0, sizeof *c);
}

uint32_t *wasm_canonical_types(const wasm_module *m) {
    uint32_t *canon = malloc((m->ntypes ? m->ntypes : 1) * sizeof *canon);
    if (!canon) return NULL;
    for (uint32_t i = 0; i < m->ntypes; i++) {
        canon[i] = i;
        const wasm_functype *a = &m->types[i];
        for (uint32_t j = 0; j < i; j++) {
            const wasm_functype *b = &m->types[j];
            if (a->nparams == b->nparams && a->nresults == b->nresults &&
                (!a->nparams || memcmp(a->params, b->params, a->nparams) == 0) &&
                (!a->nresults || memcmp(a->results, b->results, a->nresults) == 0)) {
                canon[i] = canon[j];
                break;
            }
        }
    }
    return canon;
}
