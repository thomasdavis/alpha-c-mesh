/* fleet.rs: Actual, Action, plan, seed. */
#include "mesh/fleet_plan.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <string.h>

mfl_actual mfl_actual_new(const char *name, const char *dir, uint64_t hash, bool has_prev, uint64_t prev, bool run) {
    return (mfl_actual){mt_strdup(name), mt_strdup(dir), hash, has_prev, prev, run, false, false};
}

void mfl_actual_free(mfl_actual *a) {
    free(a->name);
    free(a->dir);
    *a = (mfl_actual){0};
}

bool mfl_action_eq(const mfl_action *a, const mfl_action *b) {
    if (a->kind != b->kind || strcmp(a->name, b->name)) return false;
    switch (a->kind) {
    case MFL_LOAD: return strcmp(a->dir, b->dir) == 0 && a->hash == b->hash && a->run == b->run;
    case MFL_UPGRADE: return strcmp(a->dir, b->dir) == 0 && a->hash == b->hash;
    case MFL_RUN: return a->run == b->run;
    case MFL_MIGRATE: return strcmp(a->dir, b->dir) == 0 && a->hash == b->hash && a->run == b->run && a->from == b->from;
    default: return true;
    }
}

void mfl_actions_free(mfl_actions *a) {
    for (size_t i = 0; i < a->n; i++) {
        free(a->v[i].name);
        free(a->v[i].dir);
    }
    free(a->v);
    *a = (mfl_actions){0};
}

static void add(mfl_actions *a, mfl_kind k, const char *name, const char *dir, uint64_t hash, bool run, uint32_t from) {
    a->v = mt_realloc(a->v, (a->n + 1) * sizeof *a->v);
    a->v[a->n++] = (mfl_action){k, mt_strdup(name), dir ? mt_strdup(dir) : NULL, hash, run, from};
}

static const mfl_actual *find(const mfl_actual *actual, size_t n, const char *name) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(actual[i].name, name) == 0) return &actual[i];
    return NULL;
}

/* The source of a migration keeps the program as it is (the target takes its view, then the leader releases it). */
static bool placed(const mfd_prog *p, uint32_t me) { return mfd_prog_here(p, me) && !(p->has_from && p->from == me); }

mfl_actions mfl_plan(const mfd_doc *doc, const mfl_actual *actual, size_t nactual, mfl_skip_fn skip, void *ctx, const char *const *reset_names,
                     const uint32_t *reset_gens, size_t nresets, uint32_t me) {
    mfl_actions a = {0};
    for (size_t i = 0; i < nactual; i++) {
        const mfd_prog *p = mfd_prog_get(doc, actual[i].name);
        if (!p || !mfd_prog_here(p, me)) add(&a, MFL_UNLOAD, actual[i].name, NULL, 0, false, 0);
    }
    for (size_t i = 0; i < doc->nprogs; i++) {
        const char *n = doc->pname[i];
        const mfd_prog *p = &doc->prog[i];
        if (skip(ctx, n, p->hash) || !placed(p, me)) continue;
        const mfl_actual *x = find(actual, nactual, n);
        if (!x) {
            if (p->has_from) add(&a, MFL_MIGRATE, n, p->dir, p->hash, p->run, p->from);
            else add(&a, MFL_LOAD, n, p->dir, p->hash, p->run, 0);
        } else if (x->hash != p->hash) {
            if (x->has_prev && x->prev == p->hash) add(&a, MFL_ROLLBACK, n, NULL, 0, false, 0);
            else add(&a, MFL_UPGRADE, n, p->dir, p->hash, false, 0);
        }
    }
    for (size_t i = 0; i < doc->nprogs; i++) {
        const char *n = doc->pname[i];
        const mfd_prog *p = &doc->prog[i];
        if (skip(ctx, n, p->hash) || !placed(p, me)) continue;
        const mfl_actual *x = find(actual, nactual, n);
        if (x && (x->run != p->run || (p->run && x->failed))) add(&a, MFL_RUN, n, NULL, 0, p->run, 0);
        /* A fresh load is a reset; a lockstep program's resets are its group's epochs. */
        uint32_t applied = 0;
        for (size_t r = 0; r < nresets; r++)
            if (strcmp(reset_names[r], n) == 0) applied = reset_gens[r];
        if (!p->lockstep && x && applied < p->reset) add(&a, MFL_RESET, n, NULL, 0, false, 0);
    }
    return a;
}

mfd_doc mfl_seed(char *const *names, const mfd_prog *specs, size_t n, const ml_list *up, mfl_backend_fn backend_of, void *ctx) {
    mfd_doc d = {0};
    for (size_t i = 0; i < n; i++) mfd_prog_put(&d, names[i], mfd_prog_clone(&specs[i]));
    for (size_t i = 0; i < d.nprogs; i++) {
        const mfd_prog *p = &d.prog[i];
        if (!p->lockstep) continue;
        ml_list members = {0};
        for (size_t j = 0; j < up->n; j++)
            if (mfd_can_run(&p->backends, backend_of(ctx, up->v[j]))) ml_push(&members, up->v[j]);
        mfd_group g = {.name = mt_strdup(p->needs.group ? p->needs.group : d.pname[i]), .program = mt_strdup(d.pname[i]), .epoch = 1, .members = members,
                       .state = MFD_RUNNING, .on_loss = MFD_LOSS_PAUSE};
        mfd_group_push(&d, g);
    }
    return d;
}
