/* reconcile.rs: Rec (pass, act, migrate, group, place), cycle_ready, image_ready, run. */
#include "mesh/reconcile.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/abi/reconcile.h"
#include "mesh/strmap.h"
#include "mesh/mt.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RETRY_MS 30000

static uint64_t mono_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static const char *op_word(mfd_op op) {
    return op.kind == MFD_OP_NONE ? "none" : op.kind == MFD_OP_UPGRADE ? "upgrade" : op.kind == MFD_OP_ROLLBACK ? "rollback" : "task";
}

/* The program runs here at the group's image (the inventory after the cycle's actions). */
static bool image_ready(const mfd_group *g, uint64_t hash, const mfl_actual *inv, size_t n, char **why) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(inv[i].name, g->program) == 0) {
            if (inv[i].hash == hash) return true;
            *why = mt_fmt("group %s cycle %u (%s): %s runs %016llx here, the group %016llx; this member stays out of the cycle (paused)", g->name, g->cycle,
                          op_word(g->op), g->program, (unsigned long long)inv[i].hash, (unsigned long long)hash);
            return false;
        }
    *why = mt_fmt("group %s cycle %u: %s is not loaded here; this member stays out of the cycle (paused)", g->name, g->cycle, g->program);
    return false;
}

bool mrc_cycle_ready(const mfd_group *g, uint64_t hash, const mfl_actual *inv, size_t ninv, const bool *task_ok, const char *task_err, char **why) {
    if (!image_ready(g, hash, inv, ninv, why)) return false;
    if (g->op.kind == MFD_OP_TASK) {
        if (task_ok && !*task_ok) {
            *why = mt_fmt("group %s cycle %u: task %u %u failed here (%s); this member stays paused until the group's next epoch (`group %s reform` or `reset %s`)", g->name,
                          g->cycle, g->op.op, g->op.arg, task_err, g->name, g->program);
            return false;
        }
        if (!task_ok) {
            *why = mt_fmt("group %s cycle %u: task %u %u not run here", g->name, g->cycle, g->op.op, g->op.arg);
            return false;
        }
    }
    return true;
}

mt_strs mrc_stop_order(const mfd_doc *doc, const mt_strs *names) {
    mt_strs left = mt_strs_clone(names), out = {0};
    while (left.n) {
        size_t pick = 0;
        for (size_t i = 0; i < left.n; i++) {
            bool fed = false;
            for (size_t j = 0; j < left.n && !fed; j++) {
                if (!strcmp(left.v[j], left.v[i])) continue;
                const mfd_prog *o = mfd_prog_get(doc, left.v[j]);
                if (o && msn_strs_has(o->needs.feeds, o->needs.nfeeds, left.v[i])) fed = true;
            }
            if (!fed) {
                pick = i;
                break;
            }
        }
        mt_push(&out, left.v[pick]);
        mt_strs_remove(&left, pick);
    }
    mt_strs_free(&left);
    return out;
}

/* ---- the reconciler's memory */

typedef struct {
    char *name;
    uint64_t hash;
    char *why;
    bool unsupported;
} skip_t;

struct mrc_rec {
    mrc_ops ops;
    ms_map resets; /* name -> the reset generation applied (decimal) */
    skip_t *skip;  /* programs not to try again at this hash, sorted by name */
    size_t nskip;
    bool has_group; /* the group epoch applied here: (group, epoch, cycle, members) */
    char *g_name;
    uint32_t g_epoch, g_cycle, g_mask;
    bool has_cycle_failed; /* a cycle this member could not apply: (group, epoch, cycle, why) */
    char *cf_group, *cf_why;
    uint32_t cf_epoch, cf_cycle;
    char **place_name; /* the placement requests applied (on the leader) */
    mt_strs *place_words;
    size_t nplaces;
    mt_strs done, failed;
    mst_outcome *moves; /* per program loaded or migrated here: the outcome, sorted by name */
    size_t nmoves;
    mfd_doc doc; /* the last document */
    bool has_doc;
};

mrc_rec *mrc_new(const mrc_ops *ops) {
    mrc_rec *r = mt_alloc(sizeof *r);
    r->ops = *ops;
    return r;
}

static void skip_clear_at(mrc_rec *r, size_t i) {
    free(r->skip[i].name);
    free(r->skip[i].why);
    memmove(r->skip + i, r->skip + i + 1, (r->nskip - i - 1) * sizeof *r->skip);
    r->nskip--;
}

static void places_clear(mrc_rec *r) {
    for (size_t i = 0; i < r->nplaces; i++) {
        free(r->place_name[i]);
        mt_strs_free(&r->place_words[i]);
    }
    free(r->place_name);
    free(r->place_words);
    r->place_name = NULL;
    r->place_words = NULL;
    r->nplaces = 0;
}

static void moves_clear_at(mrc_rec *r, size_t i) {
    free(r->moves[i].name);
    memmove(r->moves + i, r->moves + i + 1, (r->nmoves - i - 1) * sizeof *r->moves);
    r->nmoves--;
}

void mrc_free(mrc_rec *r) {
    if (!r) return;
    ms_free(&r->resets);
    while (r->nskip) skip_clear_at(r, r->nskip - 1);
    free(r->skip);
    free(r->g_name);
    free(r->cf_group);
    free(r->cf_why);
    places_clear(r);
    mt_strs_free(&r->done);
    mt_strs_free(&r->failed);
    while (r->nmoves) moves_clear_at(r, r->nmoves - 1);
    free(r->moves);
    if (r->has_doc) mfd_doc_free(&r->doc);
    free(r);
}

static skip_t *skip_get(mrc_rec *r, const char *name) {
    for (size_t i = 0; i < r->nskip; i++)
        if (!strcmp(r->skip[i].name, name)) return &r->skip[i];
    return NULL;
}

static void skip_set(mrc_rec *r, const char *name, uint64_t hash, const char *why, bool unsupported) {
    skip_t *s = skip_get(r, name);
    if (s) {
        s->hash = hash;
        mt_set(&s->why, why);
        s->unsupported = unsupported;
        return;
    }
    size_t i = 0;
    while (i < r->nskip && strcmp(r->skip[i].name, name) < 0) i++;
    r->skip = mt_realloc(r->skip, (r->nskip + 1) * sizeof *r->skip);
    memmove(r->skip + i + 1, r->skip + i, (r->nskip - i) * sizeof *r->skip);
    r->skip[i] = (skip_t){mt_strdup(name), hash, mt_strdup(why), unsupported};
    r->nskip++;
}

/* skip.retain(|_, s| s.2): keep only the unsupported. */
static void skip_keep_unsupported(mrc_rec *r) {
    for (size_t i = r->nskip; i-- > 0;)
        if (!r->skip[i].unsupported) skip_clear_at(r, i);
}

static void reset_set(mrc_rec *r, const char *name, const mfd_doc *doc) {
    const mfd_prog *p = mfd_prog_get(doc, name);
    char v[16];
    snprintf(v, sizeof v, "%u", p ? p->reset : 0);
    ms_set(&r->resets, name, v, true);
}

/* A program's outcome here at doc (a move's target reports it). */
static void moved(mrc_rec *r, const mfd_doc *doc, const char *name, bool ok, uint32_t gap_us) {
    size_t i = 0;
    while (i < r->nmoves && strcmp(r->moves[i].name, name) < 0) i++;
    if (i < r->nmoves && !strcmp(r->moves[i].name, name)) {
        r->moves[i] = (mst_outcome){r->moves[i].name, doc->version, ok, gap_us};
        return;
    }
    r->moves = mt_realloc(r->moves, (r->nmoves + 1) * sizeof *r->moves);
    memmove(r->moves + i + 1, r->moves + i, (r->nmoves - i) * sizeof *r->moves);
    r->moves[i] = (mst_outcome){mt_strdup(name), doc->version, ok, gap_us};
    r->nmoves++;
}

static void logf_(mrc_rec *r, bool error, char *line) {
    if (r->ops.log) r->ops.log(r->ops.ctx, error, line);
    free(line);
}

static void status(mrc_rec *r, uint64_t applied, uint32_t st, const char *reason) {
    /* reason.chars().take(BEAT_REASON_MAX) */
    char *cut = mt_strndup(reason, strlen(reason) > MESH_BEAT_REASON_MAX ? MESH_BEAT_REASON_MAX : strlen(reason));
    r->ops.status(r->ops.ctx, applied, st, cut, r->moves, r->nmoves);
    free(cut);
}

static mfl_actual *inventory(mrc_rec *r, size_t *n) {
    mfl_actual *inv = r->ops.inventory(r->ops.ctx, n);
    return inv;
}

static void inv_free(mfl_actual *inv, size_t n) {
    for (size_t i = 0; i < n; i++) mfl_actual_free(&inv[i]);
    free(inv);
}

static const char *verb(mfl_kind k) {
    static const char *const w[] = {"unload", "load", "upgrade", "rollback", "run", "reset", "migrate"};
    return w[k];
}

static void migrate(mrc_rec *r, const mfd_doc *doc, const mfl_action *migs, size_t n);

/* One action; its result goes into done / failed / skip. */
static bool act(mrc_rec *r, const mfd_doc *doc, const mfl_action *a) {
    const char *n = a->name;
    char *text = NULL, *err = NULL, *t = NULL;
    bool ok = false;
    void *src = NULL;
    switch (a->kind) {
    case MFL_UNLOAD:
        ok = r->ops.unload(r->ops.ctx, n, &text, &err);
        if (ok) t = mt_fmt("unload %s", n);
        break;
    case MFL_LOAD:
        ok = r->ops.source(r->ops.ctx, n, a->dir, a->hash, &src, &err) && r->ops.load(r->ops.ctx, src, a->run, &text, &err);
        if (ok) {
            reset_set(r, n, doc);
            t = mt_fmt("load %s (%s)", n, text);
        }
        break;
    case MFL_UPGRADE:
        ok = r->ops.source(r->ops.ctx, n, a->dir, a->hash, &src, &err) && r->ops.upgrade(r->ops.ctx, n, src, &text, &err);
        if (ok) t = mt_fmt("upgrade %s to %016llx (%s)", n, (unsigned long long)a->hash, text);
        break;
    case MFL_ROLLBACK:
        ok = r->ops.rollback(r->ops.ctx, n, &text, &err);
        if (ok) t = mt_fmt("rollback %s (%s)", n, text);
        break;
    case MFL_RUN:
        ok = r->ops.prog_run(r->ops.ctx, n, a->run, &text, &err);
        if (ok) t = mt_fmt("%s %s", a->run ? "start" : "stop", n);
        break;
    case MFL_RESET:
        ok = r->ops.reset(r->ops.ctx, n, &text, &err);
        if (ok) {
            reset_set(r, n, doc);
            t = mt_fmt("reset %s (%s)", n, text);
        }
        break;
    case MFL_MIGRATE:
        migrate(r, doc, a, 1);
        return true;
    }
    if (src) r->ops.src_free(r->ops.ctx, src);
    free(text);
    const mfd_prog *p = mfd_prog_get(doc, n);
    uint64_t hash = p ? p->hash : 0;
    if (a->kind == MFL_LOAD) moved(r, doc, n, ok, 0);
    if (ok) {
        mt_push_own(&r->done, t);
        return true;
    }
    if (mt_starts(err, "unsupported")) {
        if (!skip_get(r, n)) logf_(r, false, mt_fmt("reconcile: %s: not on this member (%s)", n, err));
        skip_set(r, n, hash, err, true);
    } else {
        char *f = mt_fmt("%s %s: %s", verb(a->kind), n, err);
        mt_push(&r->failed, f);
        skip_set(r, n, hash, f, false);
        free(f);
    }
    free(err);
    return false;
}

/* A view from member from (the source may not have the committed document yet: retried for 3 s). */
static bool fetch_view(mrc_rec *r, const char *name, uint64_t hash, uint32_t from, uint32_t *abi, mpw_bytes *b, char **err) {
    uint64_t t0 = mono_us();
    for (;;) {
        char *e = NULL;
        if (r->ops.fetch_view(r->ops.ctx, name, hash, from, abi, b, &e)) return true;
        if (mt_contains(e, "not yet committed") && mono_us() - t0 < 3000000) {
            free(e);
            r->ops.sleep_ms(r->ops.ctx, 50);
            continue;
        }
        *err = e;
        return false;
    }
}

typedef struct {
    char *name;
    uint64_t hash;
    bool run;
    uint32_t from;
} loaded_t;

/* Programs moving here from another member: load each stopped; take each view from the source,
 * producers first; write the views (no OP_INIT); start them, consumers first. The gap is from the
 * first view request to the last start. A failure is reported (the leader puts them back). */
static void migrate(mrc_rec *r, const mfd_doc *doc, const mfl_action *migs, size_t n) {
    mt_strs names = {0};
    for (size_t i = 0; i < n; i++) mt_push(&names, migs[i].name);
    mt_strs order = mrc_stop_order(doc, &names);
    loaded_t *loaded = mt_alloc((n ? n : 1) * sizeof *loaded);
    size_t nl = 0;
    for (size_t oi = 0; oi < order.n; oi++) {
        const mfl_action *a = NULL;
        for (size_t i = 0; i < n; i++)
            if (!strcmp(migs[i].name, order.v[oi])) a = &migs[i];
        if (!a || a->kind != MFL_MIGRATE) continue;
        void *src = NULL;
        char *text = NULL, *err = NULL;
        bool ok = r->ops.source(r->ops.ctx, a->name, a->dir, a->hash, &src, &err) && r->ops.load(r->ops.ctx, src, false, &text, &err);
        if (src) r->ops.src_free(r->ops.ctx, src);
        free(text);
        if (ok) {
            reset_set(r, a->name, doc);
            loaded[nl++] = (loaded_t){a->name, a->hash, a->run, a->from};
        } else if (mt_starts(err, "unsupported")) {
            moved(r, doc, a->name, false, 0);
            skip_set(r, a->name, a->hash, err, true);
            free(err);
        } else {
            moved(r, doc, a->name, false, 0);
            mt_push_own(&r->failed, mt_fmt("migrate %s: load: %s", a->name, err));
            skip_set(r, a->name, a->hash, err, false);
            free(err);
        }
    }
    if (!nl) goto done;
    /* Any failure below fails every program loaded for this move (they stay stopped; the leader puts them back). */
    for (size_t i = 0; i < nl; i++) moved(r, doc, loaded[i].name, false, 0);
    uint64_t t0 = mono_us();
    size_t bytes = 0;
    bool ok = true;
    for (size_t i = 0; i < nl; i++) {
        uint32_t abi = 0;
        mpw_bytes v = {0};
        char *err = NULL, *text = NULL;
        if (fetch_view(r, loaded[i].name, loaded[i].hash, loaded[i].from, &abi, &v, &err)) {
            bytes += v.n;
            if (!r->ops.view_put(r->ops.ctx, loaded[i].name, abi, &v, &text, &err)) goto view_fail;
            free(text);
            mpw_bytes_free(&v);
            continue;
        }
    view_fail:
        ok = false;
        mt_push_own(&r->failed, mt_fmt("migrate %s from system %u: %s", loaded[i].name, loaded[i].from, err));
        skip_set(r, loaded[i].name, loaded[i].hash, err, false);
        free(err);
        mpw_bytes_free(&v);
    }
    if (!ok) goto done;
    double view_ms = (double)(mono_us() - t0) / 1e3;
    for (size_t i = nl; i-- > 0;) {
        if (!loaded[i].run) continue;
        char *err = NULL, *text = NULL;
        if (!r->ops.prog_run(r->ops.ctx, loaded[i].name, true, &text, &err)) {
            ok = false;
            mt_push_own(&r->failed, mt_fmt("migrate %s: start: %s", loaded[i].name, err));
            skip_set(r, loaded[i].name, loaded[i].hash, err, false);
            free(err);
        }
        free(text);
    }
    if (!ok) goto done;
    double gap = (double)(mono_us() - t0) / 1e3;
    for (size_t i = 0; i < nl; i++) moved(r, doc, loaded[i].name, true, (uint32_t)(gap * 1e3));
    char *joined = mt_join(&names, ",");
    logf_(r, false, mt_fmt("reconcile: migrated %s from system %u: views %zu bytes in %.1f ms, gap %.1f ms", joined, loaded[0].from, bytes, view_ms, gap));
    mt_push_own(&r->done, mt_fmt("migrate %s from %u: view %zu B, gap %.1f ms", joined, loaded[0].from, bytes, gap));
    free(joined);
done:
    free(loaded);
    mt_strs_free(&order);
    mt_strs_free(&names);
}

static void set_group(mrc_rec *r, const mfd_group *g) {
    mt_set(&r->g_name, g->name);
    r->g_epoch = g->epoch, r->g_cycle = g->cycle, r->g_mask = mfd_group_mask(g);
    r->has_group = true;
}

static void ready(mrc_rec *r, const mfd_group *g, uint64_t hash) {
    mpk_hello spec = {.epoch = g->epoch, .cycle = g->cycle, .mask = mfd_group_mask(g), .hash = hash};
    snprintf(spec.group, sizeof spec.group, "%s", g->name);
    mfd_group_order(g, spec.order);
    r->ops.ready(r->ops.ctx, &spec);
}

static bool loaded_at(mrc_rec *r, const char *program, uint64_t hash) {
    size_t n;
    mfl_actual *inv = inventory(r, &n);
    bool yes = false;
    for (size_t i = 0; i < n; i++) yes |= !strcmp(inv[i].name, program) && inv[i].hash == hash;
    inv_free(inv, n);
    return yes;
}

/* The lockstep program's group: a new epoch (reset), a cycle (upgrade, rollback, task), or its steps as they are. */
static void group(mrc_rec *r, const mfd_doc *doc, const mfd_group *g, const mfl_actions *acts) {
    uint32_t rank;
    bool member = mfd_group_rank(g, r->ops.me, &rank);
    const mfd_prog *p = mfd_prog_get(doc, g->program);
    uint64_t hash = p ? p->hash : 0;
    if (g->state == MFD_PAUSED || !member) {
        /* Paused, or left out of this epoch: unfed; the next epoch resets it anyway. */
        if (r->has_group || r->ops.want_set(r->ops.ctx)) r->ops.quiesce(r->ops.ctx, false, member);
        for (size_t i = 0; i < acts->n; i++) act(r, doc, &acts->v[i]);
        if (!member) r->ops.group_words(r->ops.ctx, 0, 0, g->epoch);
        bool had = r->has_group;
        r->has_group = false;
        if (had || acts->n) mt_push_own(&r->done, mt_fmt("group %s epoch %u: %s", g->name, g->epoch, !member ? "not a member here (unfed)" : "paused"));
        return;
    }
    bool same_epoch = r->has_group && !strcmp(r->g_name, g->name) && r->g_epoch == g->epoch && r->g_mask == mfd_group_mask(g);
    if (!same_epoch) {
        r->ops.quiesce(r->ops.ctx, false, true);
        for (size_t i = 0; i < acts->n; i++) act(r, doc, &acts->v[i]);
        if (!loaded_at(r, g->program, hash)) {
            char *why = mt_fmt("group %s epoch %u: %s is not loaded here at %016llx; this member cannot join", g->name, g->epoch, g->program, (unsigned long long)hash);
            skip_t *s = skip_get(r, g->program);
            if (!(s && s->unsupported)) mt_push_own(&r->failed, why);
            else mt_push_own(&r->done, why);
            return;
        }
        r->has_cycle_failed = false;
        r->ops.group_words(r->ops.ctx, rank, (uint32_t)g->members.n, g->epoch);
        char *text = NULL, *err = NULL;
        if (!r->ops.reset(r->ops.ctx, g->program, &text, &err)) {
            mt_push_own(&r->failed, mt_fmt("group %s epoch %u: reset %s: %s", g->name, g->epoch, g->program, err));
            free(err);
            return;
        }
        free(text);
        set_group(r, g);
        mt_push_own(&r->done, mt_fmt("group %s epoch %u: rank %u of %zu (reset)", g->name, g->epoch, rank, g->members.n));
        ready(r, g, hash);
    } else if (r->has_group && r->g_cycle != g->cycle) {
        /* A cycle inside the epoch: drain in step, apply, HELLO -- only once this member runs the image the
         * document names and the cycle's task ran (cycle_ready). A task that failed here earlier in this
         * epoch: a step behind for every later cycle too. */
        if (r->has_cycle_failed && !strcmp(r->cf_group, g->name) && r->cf_epoch == g->epoch && r->cf_cycle <= g->cycle) {
            mt_push(&r->failed, r->cf_why);
            return;
        }
        r->ops.quiesce(r->ops.ctx, true, true);
        for (size_t i = 0; i < acts->n; i++) act(r, doc, &acts->v[i]);
        size_t ninv;
        mfl_actual *inv = inventory(r, &ninv);
        bool ran = false, task_ok = false;
        char *task_err = NULL, *probe = NULL;
        uint32_t result = 0;
        if (g->op.kind == MFD_OP_TASK && image_ready(g, hash, inv, ninv, &probe)) {
            ran = true;
            task_ok = r->ops.task(r->ops.ctx, g->program, g->op.op, g->op.arg, &result, &task_err);
        }
        free(probe);
        char *why = NULL;
        if (!mrc_cycle_ready(g, hash, inv, ninv, ran ? &task_ok : NULL, task_err, &why)) {
            logf_(r, true, mt_fmt("reconcile: %s", why));
            /* A task that failed is not run again; a missing image is tried again (the retry, the next document). */
            if (ran) {
                r->has_cycle_failed = true;
                mt_set(&r->cf_group, g->name);
                r->cf_epoch = g->epoch, r->cf_cycle = g->cycle;
                mt_set(&r->cf_why, why);
            }
            mt_push_own(&r->failed, why);
            free(task_err);
            inv_free(inv, ninv);
            return;
        }
        free(task_err);
        inv_free(inv, ninv);
        if (ran && task_ok) mt_push_own(&r->done, mt_fmt("task %s -> %u (cycle %u)", g->program, result, g->cycle));
        r->g_cycle = g->cycle;
        mt_push_own(&r->done, mt_fmt("group %s cycle %u", g->name, g->cycle));
        ready(r, g, hash);
    } else {
        for (size_t i = 0; i < acts->n; i++) act(r, doc, &acts->v[i]);
    }
}

static const mt_strs *place_get(const mrc_rec *r, const char *name) {
    for (size_t i = 0; i < r->nplaces; i++)
        if (!strcmp(r->place_name[i], name)) return &r->place_words[i];
    return NULL;
}

static void place_set(mrc_rec *r, const char *name, const mt_strs *w) {
    for (size_t i = 0; i < r->nplaces; i++)
        if (!strcmp(r->place_name[i], name)) {
            mt_strs_free(&r->place_words[i]);
            r->place_words[i] = mt_strs_clone(w);
            return;
        }
    r->place_name = mt_realloc(r->place_name, (r->nplaces + 1) * sizeof *r->place_name);
    r->place_words = mt_realloc(r->place_words, (r->nplaces + 1) * sizeof *r->place_words);
    r->place_name[r->nplaces] = mt_strdup(name);
    r->place_words[r->nplaces++] = mt_strs_clone(w);
}

static void place_remove(mrc_rec *r, const char *name) {
    for (size_t i = 0; i < r->nplaces; i++)
        if (!strcmp(r->place_name[i], name)) {
            free(r->place_name[i]);
            mt_strs_free(&r->place_words[i]);
            r->place_name[i] = r->place_name[r->nplaces - 1];
            r->place_words[i] = r->place_words[--r->nplaces];
            return;
        }
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* The leader runs each changed placement request (the driver moves the state). */
static void place(mrc_rec *r, const mfd_doc *doc) {
    /* want: the document's requests, and `local` for one dropped from it (sorted by name). */
    mt_strs names = {0};
    for (size_t i = 0; i < doc->nplaces; i++) mt_push(&names, doc->place_name[i]);
    for (size_t i = 0; i < r->nplaces; i++)
        if (!mfd_place_get(doc, r->place_name[i]) && mfd_prog_get(doc, r->place_name[i])) mt_push(&names, r->place_name[i]);
    if (names.n > 1) qsort(names.v, names.n, sizeof *names.v, cmp_str);
    for (size_t i = 0; i < names.n; i++) {
        const mt_strs *dw = mfd_place_get(doc, names.v[i]);
        mt_strs w = dw ? mt_strs_clone(dw) : (mt_strs){0};
        if (!dw) mt_push(&w, "local");
        const mt_strs *have = place_get(r, names.v[i]);
        if (have && mt_strs_eq(have, &w)) {
            mt_strs_free(&w);
            continue;
        }
        char *text = NULL, *err = NULL, *j = mt_join(&w, " ");
        if (r->ops.place(r->ops.ctx, names.v[i], &w, &text, &err)) {
            mt_push_own(&r->done, mt_fmt("place %s %s: %s", names.v[i], j, text));
            if (w.n == 1 && !strcmp(w.v[0], "local")) place_remove(r, names.v[i]);
            else place_set(r, names.v[i], &w);
            free(text);
        } else {
            mt_push_own(&r->failed, mt_fmt("place %s %s: %s", names.v[i], j, err));
            free(err);
        }
        free(j);
        mt_strs_free(&w);
    }
    mt_strs_free(&names);
}

typedef struct {
    mrc_rec *r;
} skipctx;

static bool skip_fn(void *ctx, const char *name, uint64_t hash) {
    mrc_rec *r = ctx;
    skip_t *s = skip_get(r, name);
    return s && s->hash == hash;
}

static void pass(mrc_rec *r, const mfd_doc *doc) {
    uint32_t me = r->ops.me;
    for (size_t i = r->nmoves; i-- > 0;) {
        const mfd_prog *p = mfd_prog_get(doc, r->moves[i].name);
        if (!p || !mfd_prog_here(p, me)) moves_clear_at(r, i);
    }
    /* 1. The programs (the lockstep program's steps wait for the group, below). */
    size_t ninv;
    mfl_actual *inv = inventory(r, &ninv);
    char **rn = mt_alloc((r->resets.n ? r->resets.n : 1) * sizeof *rn);
    uint32_t *rg = mt_alloc((r->resets.n ? r->resets.n : 1) * sizeof *rg);
    for (size_t i = 0; i < r->resets.n; i++) rn[i] = r->resets.k[i], rg[i] = (uint32_t)strtoul(r->resets.v[i], NULL, 10);
    mfl_actions actions = mfl_plan(doc, inv, ninv, skip_fn, r, (const char *const *)rn, rg, r->resets.n, me);
    free(rn), free(rg);
    inv_free(inv, ninv);
    const char *lock = doc->ngroups ? doc->groups[0].program : NULL;
    mfl_actions lock_acts = {0}, migs = {0}, rest = {0};
    for (size_t i = 0; i < actions.n; i++) {
        mfl_actions *to = lock && !strcmp(actions.v[i].name, lock) ? &lock_acts : actions.v[i].kind == MFL_MIGRATE ? &migs : &rest;
        to->v = mt_realloc(to->v, (to->n + 1) * sizeof *to->v);
        to->v[to->n++] = actions.v[i];
    }
    free(actions.v);
    /* A lockstep program leaving the document: its group goes too. */
    if (!lock && r->has_group) {
        r->ops.quiesce(r->ops.ctx, false, false);
        r->ops.group_words(r->ops.ctx, 0, 0, 0);
        r->has_group = false;
    }
    for (size_t i = 0; i < rest.n; i++) act(r, doc, &rest.v[i]);
    /* Programs migrating here, with their views (the scheduler's moves). */
    if (migs.n) migrate(r, doc, migs.v, migs.n);
    /* 2. The group. */
    if (doc->ngroups) group(r, doc, &doc->groups[0], &lock_acts);
    /* 3. Placement (the leader coordinates it). */
    if (r->ops.is_leader(r->ops.ctx)) place(r, doc);
    else places_clear(r);
    /* Unsupported programs are reported, not failures. */
    char **names = mt_alloc((r->nskip ? r->nskip : 1) * sizeof *names), **whys = mt_alloc((r->nskip ? r->nskip : 1) * sizeof *whys);
    uint64_t *hashes = mt_alloc((r->nskip ? r->nskip : 1) * sizeof *hashes);
    bool *uns = mt_alloc((r->nskip ? r->nskip : 1) * sizeof *uns);
    for (size_t i = 0; i < r->nskip; i++) {
        names[i] = r->skip[i].name, hashes[i] = r->skip[i].hash, uns[i] = r->skip[i].unsupported;
        whys[i] = mt_strndup(r->skip[i].why, strlen(r->skip[i].why) > 200 ? 200 : strlen(r->skip[i].why));
    }
    r->ops.fails(r->ops.ctx, names, hashes, uns, whys, r->nskip);
    for (size_t i = 0; i < r->nskip; i++) free(whys[i]);
    free(names), free(whys), free(hashes), free(uns);
    mt_strs us = {0};
    for (size_t i = 0; i < r->nskip; i++) {
        const mfd_prog *p = mfd_prog_get(doc, r->skip[i].name);
        if (r->skip[i].unsupported && p && p->hash == r->skip[i].hash) mt_push(&us, r->skip[i].name);
    }
    if (us.n) {
        char *j = mt_join(&us, ",");
        mt_push_own(&r->done, mt_fmt("unsupported here: %s", j));
        free(j);
    }
    mt_strs_free(&us);
    mfl_actions_free(&lock_acts), mfl_actions_free(&migs), mfl_actions_free(&rest);
}

/* One pass over the current document, with its status before and after; true when it failed. */
static bool run_pass(mrc_rec *r) {
    const mfd_doc *d = &r->doc;
    char *pending = mt_fmt("reconciling version %llu", (unsigned long long)d->version);
    status(r, r->ops.applied(r->ops.ctx), MESH_RS_PENDING, pending);
    free(pending);
    mt_strs_free(&r->done);
    mt_strs_free(&r->failed);
    uint64_t t0 = mono_us();
    pass(r, d);
    double ms = (double)(mono_us() - t0) / 1e3;
    if (!r->failed.n) {
        char *done = mt_join(&r->done, ", ");
        char *s = r->done.n ? mt_fmt("ok: %s", done) : mt_strdup("ok");
        if (r->done.n) logf_(r, false, mt_fmt("reconcile: version %llu applied in %.1f ms: %s", (unsigned long long)d->version, ms, done));
        status(r, d->version, MESH_RS_SYNC, s);
        free(s), free(done);
        return false;
    }
    char *s = mt_join(&r->failed, ", "), *done = mt_join(&r->done, ", ");
    char *extra = r->done.n ? mt_fmt(" (done: %s)", done) : mt_strdup("");
    logf_(r, true, mt_fmt("reconcile: version %llu (%.1f ms): %s%s", (unsigned long long)d->version, ms, s, extra));
    status(r, d->version, MESH_RS_FAILED, s);
    free(s), free(done), free(extra);
    return true;
}

bool mrc_document(mrc_rec *r, const mfd_doc *doc) {
    if (!r->has_doc || r->doc.version != doc->version) skip_keep_unsupported(r);
    if (r->has_doc) mfd_doc_free(&r->doc);
    r->doc = mfd_doc_clone(doc);
    r->has_doc = true;
    return run_pass(r);
}

bool mrc_retry(mrc_rec *r) {
    skip_keep_unsupported(r);
    return r->has_doc ? run_pass(r) : false;
}

/* ---- the thread */

struct mrc_thread {
    pthread_t th;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    mfd_doc newest;
    bool has_newest, stop;
    mrc_rec *rec;
};

static void *thread_main(void *arg) {
    mrc_thread *t = arg;
    bool retry = false;
    struct timespec until = {0};
    for (;;) {
        pthread_mutex_lock(&t->mu);
        while (!t->has_newest && !t->stop) {
            if (!retry) pthread_cond_wait(&t->cv, &t->mu);
            else if (pthread_cond_timedwait(&t->cv, &t->mu, &until) != 0) break;
        }
        if (t->stop) {
            pthread_mutex_unlock(&t->mu);
            return NULL;
        }
        bool got = t->has_newest;
        mfd_doc d = t->newest;
        t->has_newest = false;
        t->newest = (mfd_doc){0};
        pthread_mutex_unlock(&t->mu);
        retry = got ? mrc_document(t->rec, &d) : mrc_retry(t->rec);
        if (got) mfd_doc_free(&d);
        if (retry) {
            clock_gettime(CLOCK_REALTIME, &until);
            until.tv_sec += RETRY_MS / 1000;
        }
    }
}

mrc_thread *mrc_thread_start(const mrc_ops *ops) {
    mrc_thread *t = mt_alloc(sizeof *t);
    pthread_mutex_init(&t->mu, NULL);
    pthread_cond_init(&t->cv, NULL);
    t->rec = mrc_new(ops);
    pthread_create(&t->th, NULL, thread_main, t);
    return t;
}

void mrc_post(mrc_thread *t, const mfd_doc *doc) {
    pthread_mutex_lock(&t->mu);
    if (t->has_newest) mfd_doc_free(&t->newest);
    t->newest = mfd_doc_clone(doc);
    t->has_newest = true;
    pthread_cond_signal(&t->cv);
    pthread_mutex_unlock(&t->mu);
}

void mrc_thread_stop(mrc_thread *t) {
    pthread_mutex_lock(&t->mu);
    t->stop = true;
    pthread_cond_signal(&t->cv);
    pthread_mutex_unlock(&t->mu);
    pthread_join(t->th, NULL);
    if (t->has_newest) mfd_doc_free(&t->newest);
    mrc_free(t->rec);
    pthread_mutex_destroy(&t->mu);
    pthread_cond_destroy(&t->cv);
    free(t);
}
