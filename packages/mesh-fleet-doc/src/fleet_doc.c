/* fleet.rs: ProgSpec, CycleOp, Group, Doc (encode, decode), can_run. */
#include "mesh/fleet_doc.h"
#include "mesh/abi/backend.h"
#include "mesh/parse.h"
#include "mesh/rust_fmt.h"

#include <stdlib.h>
#include <string.h>

static char *dup0(const char *s) { return s ? mt_strdup(s) : NULL; }

mfd_prog mfd_prog_new(const char *dir, uint64_t hash, bool run, const msn_needs *needs) {
    mfd_prog p = {0};
    p.dir = mt_strdup(dir);
    p.hash = hash;
    p.run = run;
    p.needs = msn_clone(needs);
    p.why = mt_strdup("");
    return p;
}

mfd_prog mfd_prog_clone(const mfd_prog *p) {
    mfd_prog c = *p;
    c.dir = mt_strdup(p->dir);
    c.backends = mt_strs_clone(&p->backends);
    c.prev_dir = dup0(p->prev_dir);
    c.needs = msn_clone(&p->needs);
    c.at = ml_opt_clone(&p->at);
    c.pin = ml_opt_clone(&p->pin);
    c.why = mt_strdup(p->why);
    return c;
}

bool mfd_prog_eq(const mfd_prog *a, const mfd_prog *b) {
    if (strcmp(a->dir, b->dir) || a->hash != b->hash || a->run != b->run || a->reset != b->reset || a->lockstep != b->lockstep ||
        !mt_strs_eq(&a->backends, &b->backends) || a->has_prev != b->has_prev)
        return false;
    if (a->has_prev && (strcmp(a->prev_dir, b->prev_dir) || a->prev_hash != b->prev_hash)) return false;
    if (!msn_eq(&a->needs, &b->needs) || !ml_opt_eq(&a->at, &b->at) || a->has_from != b->has_from || (a->has_from && a->from != b->from)) return false;
    if (a->has_to != b->has_to || (a->has_to && a->to != b->to) || a->since != b->since || !ml_opt_eq(&a->pin, &b->pin)) return false;
    return strcmp(a->why, b->why) == 0;
}

void mfd_prog_free(mfd_prog *p) {
    free(p->dir);
    mt_strs_free(&p->backends);
    free(p->prev_dir);
    msn_free(&p->needs);
    ml_opt_free(&p->at);
    ml_opt_free(&p->pin);
    free(p->why);
    *p = (mfd_prog){0};
}

bool mfd_prog_here(const mfd_prog *p, uint32_t k) { return ml_opt_has_or_all(&p->at, k) || (p->has_from && p->from == k); }

uint32_t mfd_group_mask(const mfd_group *g) { return ml_mask(&g->members); }

bool mfd_group_rank(const mfd_group *g, uint32_t k, uint32_t *rank) {
    long i = ml_find(&g->members, k);
    if (i < 0) return false;
    *rank = (uint32_t)i;
    return true;
}

void mfd_group_order(const mfd_group *g, uint8_t out[MESH_MAX_SYSTEMS]) {
    memset(out, 0xff, MESH_MAX_SYSTEMS);
    for (size_t r = 0; r < g->members.n && r < MESH_MAX_SYSTEMS; r++) out[r] = (uint8_t)g->members.v[r];
}

mfd_group mfd_group_clone(const mfd_group *g) {
    mfd_group c = *g;
    c.name = mt_strdup(g->name);
    c.program = mt_strdup(g->program);
    c.members = ml_clone(&g->members);
    return c;
}

bool mfd_group_eq(const mfd_group *a, const mfd_group *b) {
    return strcmp(a->name, b->name) == 0 && strcmp(a->program, b->program) == 0 && a->epoch == b->epoch && a->cycle == b->cycle && a->op.kind == b->op.kind &&
           (a->op.kind != MFD_OP_TASK || (a->op.op == b->op.op && a->op.arg == b->op.arg)) && ml_eq(&a->members, &b->members) && a->state == b->state &&
           a->on_loss == b->on_loss && a->auto_ms == b->auto_ms;
}

void mfd_group_free(mfd_group *g) {
    free(g->name);
    free(g->program);
    ml_free(&g->members);
    *g = (mfd_group){0};
}

char *mfd_op_word(mfd_op op) {
    switch (op.kind) {
    case MFD_OP_NONE: return mt_strdup("none");
    case MFD_OP_UPGRADE: return mt_strdup("upgrade");
    case MFD_OP_ROLLBACK: return mt_strdup("rollback");
    default: return mt_fmt("task:%u:%u", op.op, op.arg);
    }
}

bool mfd_op_parse(const char *s, mfd_op *out, char **err) {
    if (strcmp(s, "none") == 0) return *out = (mfd_op){MFD_OP_NONE, 0, 0}, true;
    if (strcmp(s, "upgrade") == 0) return *out = (mfd_op){MFD_OP_UPGRADE, 0, 0}, true;
    if (strcmp(s, "rollback") == 0) return *out = (mfd_op){MFD_OP_ROLLBACK, 0, 0}, true;
    mt_strs p = mt_split(s, ':');
    uint32_t o, a;
    bool ok = p.n == 3 && strcmp(p.v[0], "task") == 0 && mp_u32(p.v[1], &o) && mp_u32(p.v[2], &a);
    mt_strs_free(&p);
    if (ok) return *out = (mfd_op){MFD_OP_TASK, o, a}, true;
    char *q = rf_debug_str(s);
    *err = mt_fmt("bad op %s", q);
    free(q);
    return false;
}

int mfd_id_cmp(uint64_t at, uint64_t av, uint64_t bt, uint64_t bv) {
    if (at != bt) return at < bt ? -1 : 1;
    if (av != bv) return av < bv ? -1 : 1;
    return 0;
}

/* ---- the document's maps */

static size_t find_name(char **names, size_t n, const char *key, bool *found) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(names[mid], key);
        if (!c) {
            *found = true;
            return mid;
        }
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    *found = false;
    return lo;
}

long mfd_prog_index(const mfd_doc *d, const char *name) {
    bool f;
    size_t i = find_name(d->pname, d->nprogs, name, &f);
    return f ? (long)i : -1;
}

mfd_prog *mfd_prog_get(const mfd_doc *d, const char *name) {
    long i = mfd_prog_index(d, name);
    return i < 0 ? NULL : &d->prog[i];
}

void mfd_prog_put(mfd_doc *d, const char *name, mfd_prog p) {
    bool f;
    size_t i = find_name(d->pname, d->nprogs, name, &f);
    if (f) {
        mfd_prog_free(&d->prog[i]);
        d->prog[i] = p;
        return;
    }
    d->pname = mt_realloc(d->pname, (d->nprogs + 1) * sizeof *d->pname);
    d->prog = mt_realloc(d->prog, (d->nprogs + 1) * sizeof *d->prog);
    memmove(d->pname + i + 1, d->pname + i, (d->nprogs - i) * sizeof *d->pname);
    memmove(d->prog + i + 1, d->prog + i, (d->nprogs - i) * sizeof *d->prog);
    d->pname[i] = mt_strdup(name);
    d->prog[i] = p;
    d->nprogs++;
}

bool mfd_prog_remove(mfd_doc *d, const char *name) {
    long i = mfd_prog_index(d, name);
    if (i < 0) return false;
    free(d->pname[i]);
    mfd_prog_free(&d->prog[i]);
    memmove(d->pname + i, d->pname + i + 1, (d->nprogs - (size_t)i - 1) * sizeof *d->pname);
    memmove(d->prog + i, d->prog + i + 1, (d->nprogs - (size_t)i - 1) * sizeof *d->prog);
    d->nprogs--;
    return true;
}

mfd_group *mfd_group_of(const mfd_doc *d, const char *program) {
    for (size_t i = 0; i < d->ngroups; i++)
        if (strcmp(d->groups[i].program, program) == 0) return &d->groups[i];
    return NULL;
}

mfd_group *mfd_group_named(const mfd_doc *d, const char *name) {
    for (size_t i = 0; i < d->ngroups; i++)
        if (strcmp(d->groups[i].name, name) == 0) return &d->groups[i];
    return NULL;
}

void mfd_group_push(mfd_doc *d, mfd_group g) {
    d->groups = mt_realloc(d->groups, (d->ngroups + 1) * sizeof *d->groups);
    d->groups[d->ngroups++] = g;
}

void mfd_group_remove_at(mfd_doc *d, size_t i) {
    mfd_group_free(&d->groups[i]);
    memmove(d->groups + i, d->groups + i + 1, (d->ngroups - i - 1) * sizeof *d->groups);
    d->ngroups--;
}

const mt_strs *mfd_place_get(const mfd_doc *d, const char *name) {
    bool f;
    size_t i = find_name(d->place_name, d->nplaces, name, &f);
    return f ? &d->place_words[i] : NULL;
}

void mfd_place_put(mfd_doc *d, const char *name, mt_strs words) {
    bool f;
    size_t i = find_name(d->place_name, d->nplaces, name, &f);
    if (f) {
        mt_strs_free(&d->place_words[i]);
        d->place_words[i] = words;
        return;
    }
    d->place_name = mt_realloc(d->place_name, (d->nplaces + 1) * sizeof *d->place_name);
    d->place_words = mt_realloc(d->place_words, (d->nplaces + 1) * sizeof *d->place_words);
    memmove(d->place_name + i + 1, d->place_name + i, (d->nplaces - i) * sizeof *d->place_name);
    memmove(d->place_words + i + 1, d->place_words + i, (d->nplaces - i) * sizeof *d->place_words);
    d->place_name[i] = mt_strdup(name);
    d->place_words[i] = words;
    d->nplaces++;
}

bool mfd_place_remove(mfd_doc *d, const char *name) {
    bool f;
    size_t i = find_name(d->place_name, d->nplaces, name, &f);
    if (!f) return false;
    free(d->place_name[i]);
    mt_strs_free(&d->place_words[i]);
    memmove(d->place_name + i, d->place_name + i + 1, (d->nplaces - i - 1) * sizeof *d->place_name);
    memmove(d->place_words + i, d->place_words + i + 1, (d->nplaces - i - 1) * sizeof *d->place_words);
    d->nplaces--;
    return true;
}

mfd_doc mfd_doc_clone(const mfd_doc *d) {
    mfd_doc c = {.version = d->version, .term = d->term, .sched_paused = d->sched_paused, .autoreform = d->autoreform, .cordon = d->cordon};
    for (size_t i = 0; i < d->nprogs; i++) mfd_prog_put(&c, d->pname[i], mfd_prog_clone(&d->prog[i]));
    for (size_t i = 0; i < d->ngroups; i++) mfd_group_push(&c, mfd_group_clone(&d->groups[i]));
    for (size_t i = 0; i < d->nplaces; i++) mfd_place_put(&c, d->place_name[i], mt_strs_clone(&d->place_words[i]));
    return c;
}

bool mfd_doc_eq(const mfd_doc *a, const mfd_doc *b) {
    if (a->version != b->version || a->term != b->term || a->nprogs != b->nprogs || a->ngroups != b->ngroups || a->nplaces != b->nplaces ||
        a->sched_paused != b->sched_paused || a->autoreform != b->autoreform || a->cordon != b->cordon)
        return false;
    for (size_t i = 0; i < a->nprogs; i++)
        if (strcmp(a->pname[i], b->pname[i]) || !mfd_prog_eq(&a->prog[i], &b->prog[i])) return false;
    for (size_t i = 0; i < a->ngroups; i++)
        if (!mfd_group_eq(&a->groups[i], &b->groups[i])) return false;
    for (size_t i = 0; i < a->nplaces; i++)
        if (strcmp(a->place_name[i], b->place_name[i]) || !mt_strs_eq(&a->place_words[i], &b->place_words[i])) return false;
    return true;
}

void mfd_doc_free(mfd_doc *d) {
    for (size_t i = 0; i < d->nprogs; i++) {
        free(d->pname[i]);
        mfd_prog_free(&d->prog[i]);
    }
    for (size_t i = 0; i < d->ngroups; i++) mfd_group_free(&d->groups[i]);
    for (size_t i = 0; i < d->nplaces; i++) {
        free(d->place_name[i]);
        mt_strs_free(&d->place_words[i]);
    }
    free(d->pname);
    free(d->prog);
    free(d->groups);
    free(d->place_name);
    free(d->place_words);
    *d = (mfd_doc){0};
}

/* ---- text */

static char *opt_list(const ml_opt *o) { return o->some ? ml_text(&o->l) : mt_strdup("*"); }

char *mfd_encode(const mfd_doc *d) {
    mt_buf b = {0};
    mt_printf(&b, "doc version %llu term %llu\n", (unsigned long long)d->version, (unsigned long long)d->term);
    for (size_t i = 0; i < d->nprogs; i++) {
        const mfd_prog *p = &d->prog[i];
        char *prev = p->has_prev ? mt_fmt("%s %016llx", p->prev_dir, (unsigned long long)p->prev_hash) : mt_strdup("- 0");
        char *back = p->backends.n ? mt_join(&p->backends, ",") : mt_strdup("-");
        char *at = opt_list(&p->at), *pin = opt_list(&p->pin);
        char *from = p->has_from && p->has_to ? mt_fmt("%u>%u@%llu", p->from, p->to, (unsigned long long)p->since)
                     : p->has_from            ? mt_fmt("%u", p->from)
                                              : mt_strdup("-");
        char *needs = msn_encode(&p->needs);
        char *why = mt_strdup(p->why);
        for (char *c = why; *c; c++)
            if (*c == '\n') *c = ' ';
        mt_printf(&b, "program %s dir %s hash %016llx run %u reset %u lockstep %u backends %s prev %s at %s from %s pin %s needs %s why %s\n", d->pname[i],
                  p->dir, (unsigned long long)p->hash, (unsigned)p->run, p->reset, (unsigned)p->lockstep, back, prev, at, from, pin, needs, why);
        free(prev), free(back), free(at), free(pin), free(from), free(needs), free(why);
    }
    for (size_t i = 0; i < d->ngroups; i++) {
        const mfd_group *g = &d->groups[i];
        char *op = mfd_op_word(g->op), *m = ml_text(&g->members);
        mt_printf(&b, "group %s program %s epoch %u cycle %u op %s members %s state %s on_loss %s auto_ms %u\n", g->name, g->program, g->epoch, g->cycle, op, m,
                  g->state == MFD_RUNNING ? "running" : "paused", g->on_loss == MFD_LOSS_PAUSE ? "pause" : "reform", g->auto_ms);
        free(op), free(m);
    }
    for (size_t i = 0; i < d->nplaces; i++) {
        char *w = mt_join(&d->place_words[i], ",");
        mt_printf(&b, "place %s %s\n", d->place_name[i], w);
        free(w);
    }
    ml_list c = ml_members(d->cordon);
    char *ct = ml_text(&c);
    mt_printf(&b, "sched paused %u autoreform %u cordon %s\n", (unsigned)d->sched_paused, (unsigned)d->autoreform, ct);
    free(ct);
    ml_free(&c);
    return mt_take(&b);
}

static char *bad_line(size_t i, const char *line) {
    char *q = rf_debug_str(line);
    char *e = mt_fmt("fleet document line %zu: %s", i + 1, q);
    free(q);
    return e;
}

static bool opt_parse(const char *v, ml_opt *out, char **err) {
    if (strcmp(v, "*") == 0) return *out = ml_none(), true;
    out->some = true;
    return ml_parse(v, &out->l, err);
}

static bool words_are(mt_strs *w, size_t n, const char *const *lit) {
    if (w->n != n) return false;
    for (size_t i = 0; i < n; i++)
        if (lit[i] && strcmp(w->v[i], lit[i])) return false;
    return true;
}

static bool program_line(mfd_doc *d, size_t li, const char *line, mt_strs *w, char **err) {
    char **v = w->v;
    mfd_prog p = {0};
    uint64_t u;
    /* The Rust's evaluation order (it decides which error a line with several gets): prev, the
     * migration, then the fields as ProgSpec lists them. */
    if (strcmp(v[15], "-") != 0) {
        p.has_prev = true;
        p.prev_dir = mt_strdup(v[15]);
        if (!mp_hex_u64(v[16], &p.prev_hash)) goto bad;
    }
    if (strcmp(v[13], "-") != 0) p.backends = mt_split(v[13], ',');
    const char *fr = v[20];
    if (strcmp(fr, "-") != 0) {
        const char *gt = strchr(fr, '>');
        if (!gt) {
            if (!mp_u64(fr, &u)) goto bad;
            p.has_from = true;
            p.from = (uint32_t)u;
        } else {
            char *f = mt_strndup(fr, (size_t)(gt - fr));
            const char *at = strchr(gt + 1, '@');
            char *t = at ? mt_strndup(gt + 1, (size_t)(at - gt - 1)) : NULL;
            uint64_t fv, tv, sv;
            bool ok = at && mp_u64(f, &fv) && mp_u64(t, &tv) && mp_u64(at + 1, &sv);
            free(f);
            free(t);
            if (!ok) goto bad;
            p.has_from = p.has_to = true;
            p.from = (uint32_t)fv;
            p.to = (uint32_t)tv;
            p.since = sv;
        }
    }
    if (!mp_hex_u64(v[5], &p.hash)) goto bad;
    p.run = strcmp(v[7], "1") == 0;
    if (!mp_u64(v[9], &u)) goto bad;
    p.reset = (uint32_t)u;
    p.lockstep = strcmp(v[11], "1") == 0;
    if (!msn_decode(v[24], &p.needs, err)) goto fail;
    if (!opt_parse(v[18], &p.at, err)) goto fail;
    if (!opt_parse(v[22], &p.pin, err)) goto fail;
    const char *wy = strstr(line, " why ");
    p.why = wy ? mt_trim(wy + 5) : mt_strdup("");
    p.dir = mt_strdup(v[3]);
    mfd_prog_put(d, v[1], p);
    return true;
bad:
    *err = bad_line(li, line);
fail:
    if (!p.why) p.why = mt_strdup("");
    if (!p.dir) p.dir = mt_strdup("");
    mfd_prog_free(&p);
    return false;
}

static bool is_program(mt_strs *w) {
    static const char *const keys[] = {"program", NULL, "dir", NULL, "hash", NULL, "run", NULL, "reset", NULL, "lockstep", NULL, "backends", NULL,
                                       "prev", NULL, NULL, "at", NULL, "from", NULL, "pin", NULL, "needs", NULL, "why"};
    if (w->n < 26) return false;
    for (size_t i = 0; i < 26; i++)
        if (keys[i] && strcmp(w->v[i], keys[i])) return false;
    return true;
}

bool mfd_decode(const char *text, mfd_doc *out, char **err) {
    static const char *const head[] = {"doc", "version", NULL, "term", NULL};
    static const char *const group[] = {"group", NULL, "program", NULL, "epoch", NULL, "cycle", NULL, "op", NULL, "members", NULL, "state", NULL, "on_loss", NULL, "auto_ms", NULL};
    static const char *const place[] = {"place", NULL, NULL};
    static const char *const sched[] = {"sched", "paused", NULL, "autoreform", NULL, "cordon", NULL};
    mfd_doc d = {0};
    bool have_head = false, ok = true;
    mt_strs ls = mt_lines(text);
    for (size_t i = 0; i < ls.n && ok; i++) {
        const char *line = ls.v[i];
        mt_strs w = mt_words(line);
        char **v = w.v;
        if (w.n == 0) {
        } else if (words_are(&w, 5, head)) {
            ok = mp_u64(v[2], &d.version) && mp_u64(v[4], &d.term);
            if (!ok) *err = bad_line(i, line);
            have_head = true;
        } else if (is_program(&w)) {
            ok = program_line(&d, i, line, &w, err);
        } else if (words_are(&w, 18, group)) {
            mfd_group g = {0};
            uint64_t e, c, am;
            if (!mp_u64(v[5], &e) || !mp_u64(v[7], &c)) {
                *err = bad_line(i, line);
                ok = false;
            } else if (!mfd_op_parse(v[9], &g.op, err) || !ml_parse(v[11], &g.members, err)) {
                ok = false;
            } else if ((strcmp(v[13], "running") && strcmp(v[13], "paused")) || (strcmp(v[15], "pause") && strcmp(v[15], "reform")) || !mp_u64(v[17], &am)) {
                *err = bad_line(i, line);
                ok = false;
                ml_free(&g.members);
            } else {
                g.name = mt_strdup(v[1]);
                g.program = mt_strdup(v[3]);
                g.epoch = (uint32_t)e;
                g.cycle = (uint32_t)c;
                g.state = strcmp(v[13], "running") == 0 ? MFD_RUNNING : MFD_PAUSED;
                g.on_loss = strcmp(v[15], "pause") == 0 ? MFD_LOSS_PAUSE : MFD_LOSS_REFORM;
                g.auto_ms = (uint32_t)am;
                mfd_group_push(&d, g);
            }
        } else if (words_are(&w, 3, place)) {
            mfd_place_put(&d, v[1], mt_split(v[2], ','));
        } else if (words_are(&w, 7, sched)) {
            ml_list c;
            if (ml_parse(v[6], &c, err)) {
                d.sched_paused = strcmp(v[2], "1") == 0;
                d.autoreform = strcmp(v[4], "1") == 0;
                d.cordon = ml_mask(&c);
                ml_free(&c);
            } else ok = false;
        } else {
            *err = bad_line(i, line);
            ok = false;
        }
        mt_strs_free(&w);
    }
    mt_strs_free(&ls);
    if (ok && !have_head) {
        *err = mt_strdup("fleet document: no `doc version .. term ..` line");
        ok = false;
    }
    if (!ok) {
        mfd_doc_free(&d);
        return false;
    }
    *out = d;
    return true;
}

bool mfd_can_run(const mt_strs *backends, uint32_t kind) {
    const char *me = kind < MESH_BACKENDS_LEN ? MESH_BACKENDS[kind] : "?";
    for (size_t i = 0; i < backends->n; i++)
        if (strcmp(backends->v[i], me) == 0 || strcmp(backends->v[i], "wasm") == 0 || strcmp(backends->v[i], "native") == 0) return true;
    return false;
}
