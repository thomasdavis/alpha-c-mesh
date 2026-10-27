/* fleet.rs: Req (parse), Image, EditCtx, edit, revert. */
#include "mesh/fleet_edit.h"
#include "mesh/abi/base.h"
#include "mesh/abi/placement.h"
#include "mesh/fleet_groups.h"
#include "mesh/parse.h"
#include "mesh/place_mode.h"
#include "mesh/rust_fmt.h"

#include <stdlib.h>
#include <string.h>

void mfe_req_free(mfe_req *r) {
    free(r->name);
    free(r->dir);
    mt_strs_free(&r->words);
    ml_opt_free(&r->pin);
    *r = (mfe_req){0};
}

void mfe_image_free(mfe_image *im) {
    free(im->dir);
    free(im->name);
    mt_strs_free(&im->backends);
    mesh_needs_free(&im->needs);
    *im = (mfe_image){0};
}

/* "0x.." in hex, else decimal (fleet.rs int). */
static bool int_word(const char *s, uint32_t *out) { return mt_starts(s, "0x") ? mp_hex_u32(s + 2, out) : mp_u32(s, out); }

static bool w_is(const mt_strs *w, size_t n, const char *const *lit) {
    if (w->n != n) return false;
    for (size_t i = 0; i < n; i++)
        if (lit[i] && strcmp(w->v[i], lit[i])) return false;
    return true;
}

#define IS(...) w_is(&w, sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *), (const char *const[]){__VA_ARGS__})

bool mfe_parse(const char *line, mfe_req *out, char **err) {
    mt_strs w = mt_words(line);
    mfe_req r = {0};
    bool ok = true;
    char **v = w.v;
    if (IS("load", NULL)) r.kind = MFE_LOAD, r.name = mt_strdup(v[1]);
    else if (IS("upgrade", NULL)) r.kind = MFE_UPGRADE, r.name = mt_strdup(v[1]);
    else if (IS("upgrade", NULL, NULL)) r.kind = MFE_UPGRADE, r.name = mt_strdup(v[1]), r.dir = mt_strdup(v[2]);
    else if (IS("rollback", NULL)) r.kind = MFE_ROLLBACK, r.name = mt_strdup(v[1]);
    else if (IS("unload", NULL) || IS("unload", NULL, "--now")) r.kind = MFE_UNLOAD, r.name = mt_strdup(v[1]);
    else if (IS("start", NULL)) r.kind = MFE_RUN, r.name = mt_strdup(v[1]), r.on = true;
    else if (IS("stop", NULL)) r.kind = MFE_RUN, r.name = mt_strdup(v[1]), r.on = false;
    else if (IS("reset", NULL)) r.kind = MFE_RESET, r.name = mt_strdup(v[1]);
    else if (IS("task", NULL, NULL, NULL)) {
        r.kind = MFE_TASK;
        if (!int_word(v[2], &r.op) || !int_word(v[3], &r.arg)) {
            *err = mt_strdup("usage: task <name> <op> <arg>");
            ok = false;
        } else r.name = mt_strdup(v[1]);
    } else if (w.n >= 3 && strcmp(v[0], "place") == 0) {
        r.kind = MFE_PLACE;
        r.name = mt_strdup(v[1]);
        for (size_t i = 2; i < w.n; i++) mt_push(&r.words, v[i]);
    } else if (IS("group", NULL, "reform")) r.kind = MFE_REFORM, r.name = mt_strdup(v[1]);
    else if (IS("group", NULL, "on_loss", "pause")) r.kind = MFE_ON_LOSS, r.name = mt_strdup(v[1]), r.on_loss = MFD_LOSS_PAUSE;
    else if (IS("group", NULL, "on_loss", "reform")) r.kind = MFE_ON_LOSS, r.name = mt_strdup(v[1]), r.on_loss = MFD_LOSS_REFORM;
    else if (IS("group", NULL, "on_loss", "reform", NULL)) {
        r.kind = MFE_ON_LOSS, r.on_loss = MFD_LOSS_REFORM;
        if (!mp_u32(v[4], &r.ms)) {
            char *q = rf_debug_str(v[4]);
            *err = mt_fmt("%s: milliseconds", q);
            free(q);
            ok = false;
        } else r.name = mt_strdup(v[1]);
    } else if (IS("sched", "pin", NULL, NULL)) {
        r.kind = MFE_PIN;
        char *e = NULL;
        r.pin.some = true;
        if (!ml_parse(v[3], &r.pin.l, &e)) {
            *err = mt_fmt("usage: sched pin <name> <member[,member..]>: %s", e);
            free(e);
            ok = false;
        } else r.name = mt_strdup(v[2]);
    } else if (IS("sched", "unpin", NULL)) r.kind = MFE_PIN, r.name = mt_strdup(v[2]);
    else if (IS("sched", "pause")) r.kind = MFE_SCHED_PAUSE, r.on = true;
    else if (IS("sched", "resume")) r.kind = MFE_SCHED_PAUSE, r.on = false;
    else if (IS("sched", "autoreform", "on")) r.kind = MFE_AUTOREFORM, r.on = true;
    else if (IS("sched", "autoreform", "off")) r.kind = MFE_AUTOREFORM, r.on = false;
    else if (IS("sched", "cordon", NULL) || IS("sched", "uncordon", NULL)) {
        r.kind = MFE_CORDON;
        r.on = strcmp(v[1], "cordon") == 0;
        if (!mp_u32(v[2], &r.k) || r.k >= MESH_MAX_SYSTEMS) {
            char *q = rf_debug_str(v[2]);
            *err = mt_fmt("%s: a member", q);
            free(q);
            ok = false;
        }
    } else {
        char *q = rf_debug_str(line);
        *err = mt_fmt("not a change: %s", q);
        free(q);
        ok = false;
    }
    mt_strs_free(&w);
    if (!ok) {
        mfe_req_free(&r);
        return false;
    }
    *out = r;
    return true;
}

static char *missing(const char *n) {
    char *q = rf_debug_str(n);
    char *e = mt_fmt("no program %s in the desired state (`fleet` lists it)", q);
    free(q);
    return e;
}

static char *no_group(const char *n) {
    char *q = rf_debug_str(n);
    char *e = mt_fmt("no group %s (`groups` lists them)", q);
    free(q);
    return e;
}

static mfd_group *running_group(mfd_doc *d, const char *program) {
    for (size_t i = 0; i < d->ngroups; i++)
        if (strcmp(d->groups[i].program, program) == 0 && d->groups[i].state == MFD_RUNNING) return &d->groups[i];
    return NULL;
}

static bool load(mfd_doc *d, const mfe_req *req, const mfe_ctx *cx, char **what, char **err) {
    mfe_image im;
    if (!cx->catalog(cx->ctx, req->name, &im, err)) return false;
    bool ok = false;
    if (mfd_prog_get(d, im.name)) {
        *err = mt_fmt("name exists: %s is in the desired state", im.name);
        goto done;
    }
    if (im.lockstep) {
        for (size_t i = 0; i < d->nprogs; i++)
            if (d->prog[i].lockstep) {
                *err = mt_fmt("manifest: %s is the lockstep program already; only one may be loaded", d->pname[i]);
                goto done;
            }
        ml_list members = cx->eligible_up(cx->ctx, &im.backends);
        if (!members.n) {
            char *b = mt_join(&im.backends, ", ");
            *err = mt_fmt("unsupported: no up member runs %s (backends %s)", im.name, b);
            free(b);
            ml_free(&members);
            goto done;
        }
        const char *gname = im.needs.group ? im.needs.group : im.name;
        uint32_t epoch = 0;
        for (size_t i = 0; i < d->ngroups; i++)
            if (strcmp(d->groups[i].name, gname) == 0 && d->groups[i].epoch > epoch) epoch = d->groups[i].epoch;
        for (size_t i = d->ngroups; i-- > 0;)
            if (strcmp(d->groups[i].name, gname) == 0) mfd_group_remove_at(d, i);
        mfd_group_push(d, (mfd_group){.name = mt_strdup(gname), .program = mt_strdup(im.name), .epoch = epoch + 1, .members = members, .state = MFD_RUNNING,
                                      .on_loss = MFD_LOSS_PAUSE});
    }
    mfd_prog p = mfd_prog_new(im.dir, im.hash, im.autostart, &im.needs);
    p.lockstep = im.lockstep;
    p.backends = mt_strs_clone(&im.backends);
    mt_set(&p.why, "new: the scheduler places it");
    mfd_prog_put(d, im.name, p);
    *what = mt_fmt("load %s from %s (%016llx)", im.name, im.dir, (unsigned long long)im.hash);
    ok = true;
done:
    mfe_image_free(&im);
    return ok;
}

static bool upgrade(mfd_doc *d, const mfe_req *req, const mfe_ctx *cx, char **what, char **err) {
    const char *n = req->name;
    mfd_prog *cur = mfd_prog_get(d, n);
    if (!cur) return *err = missing(n), false;
    mfd_prog p = mfd_prog_clone(cur);
    mfe_image im;
    bool ok = false;
    if (!cx->catalog(cx->ctx, req->dir ? req->dir : p.dir, &im, err)) {
        mfd_prog_free(&p);
        return false;
    }
    if (strcmp(im.name, n) != 0) {
        char *a = rf_debug_str(im.name), *b = rf_debug_str(n);
        *err = mt_fmt("manifest: %s names %s, not %s", im.dir, a, b);
        free(a), free(b);
        goto done;
    }
    if (im.lockstep != p.lockstep) {
        *err = mt_fmt("manifest: %s: an upgrade cannot change `peer` (unload and load it)", im.dir);
        goto done;
    }
    if (im.hash == p.hash && strcmp(im.dir, p.dir) == 0) {
        *what = mt_fmt("upgrade %s: already at %016llx (%s): nothing changes", n, (unsigned long long)p.hash, p.dir);
        ok = true;
        goto done;
    }
    /* A running lockstep group upgrades in one cycle on every member: each must be able to run the new image. */
    mfd_group *g = running_group(d, n);
    if (g) {
        ml_list can = cx->eligible_up(cx->ctx, &im.backends), cannot = {0};
        for (size_t i = 0; i < g->members.n; i++)
            if (!ml_has(&can, g->members.v[i])) ml_push(&cannot, g->members.v[i]);
        ml_free(&can);
        if (cannot.n) {
            char *l = ml_text(&cannot), *b = mt_join(&im.backends, ",");
            *err = mt_fmt("unsupported: group %s member(s) %s cannot run %s (backends %s): every member of a lockstep group runs the same image (`group %s reform` "
                          "without them first)",
                          g->name, l, im.dir, b, g->name);
            free(l), free(b);
            ml_free(&cannot);
            goto done;
        }
    }
    mfd_prog *e = mfd_prog_get(d, n);
    e->has_prev = true;
    mt_set(&e->prev_dir, p.dir);
    e->prev_hash = p.hash;
    mt_set(&e->dir, im.dir);
    e->hash = im.hash;
    mt_strs_free(&e->backends);
    e->backends = mt_strs_clone(&im.backends);
    mesh_needs_free(&e->needs);
    e->needs = msn_clone(&im.needs);
    if (g) {
        g->cycle += 1;
        g->op = (mfd_op){MFD_OP_UPGRADE, 0, 0};
    }
    *what = mt_fmt("upgrade %s %016llx -> %016llx (%s)", n, (unsigned long long)p.hash, (unsigned long long)im.hash, im.dir);
    ok = true;
done:
    mfe_image_free(&im);
    mfd_prog_free(&p);
    return ok;
}

static bool place(mfd_doc *d, const mfe_req *req, const mfe_ctx *cx, char **what, char **err) {
    const char *n = req->name;
    mfd_prog *p = mfd_prog_get(d, n);
    if (!p) return *err = missing(n), false;
    mfe_image im;
    if (!cx->catalog(cx->ctx, p->dir, &im, err)) return false;
    bool ok = false;
    char *e = NULL;
    if (!im.placeable) {
        *err = mt_fmt("%s declares no `place` in its manifest", n);
        goto done;
    }
    mpm_target t;
    if (!mpm_parse(req->words.v, req->words.n, cx->n, im.place_fanout, &t, err)) goto done;
    mpm_mode m = t.is_auto ? mpm_split(mpm_even((uint32_t[]){0}, 1, im.place_fanout)) : t.mode;
    if (!mpm_allowed(&m, im.place_cap, &e)) {
        *err = mt_fmt("%s: %s", n, e);
        free(e);
        goto done;
    }
    if (req->words.n == 1 && strcmp(req->words.v[0], "local") == 0) mfd_place_remove(d, n);
    else mfd_place_put(d, n, mt_strs_clone(&req->words));
    char *j = mt_join(&req->words, " ");
    *what = mt_fmt("place %s %s", n, j);
    free(j);
    ok = true;
done:
    mfe_image_free(&im);
    return ok;
}

bool mfe_edit(const mfd_doc *doc, const mfe_req *req, const mfe_ctx *cx, mfd_doc *out, char **what, char **err) {
    mfd_doc d = mfd_doc_clone(doc);
    bool ok = true;
    const char *n = req->name;
    switch (req->kind) {
    case MFE_LOAD: ok = load(&d, req, cx, what, err); break;
    case MFE_UPGRADE: ok = upgrade(&d, req, cx, what, err); break;
    case MFE_ROLLBACK: {
        mfd_prog *e = mfd_prog_get(&d, n);
        if (!e) {
            *err = missing(n);
            ok = false;
            break;
        }
        if (!e->has_prev) {
            *err = mt_fmt("%s: no previous image to roll back to", n);
            ok = false;
            break;
        }
        char *pd = mt_strdup(e->prev_dir), *od = mt_strdup(e->dir);
        uint64_t ph = e->prev_hash, oh = e->hash;
        mt_set(&e->prev_dir, od);
        e->prev_hash = oh;
        mt_set(&e->dir, pd);
        e->hash = ph;
        free(pd), free(od);
        mfd_group *g = running_group(&d, n);
        if (g) {
            g->cycle += 1;
            g->op = (mfd_op){MFD_OP_ROLLBACK, 0, 0};
        }
        *what = mt_fmt("rollback %s %016llx -> %016llx", n, (unsigned long long)oh, (unsigned long long)ph);
        break;
    }
    case MFE_UNLOAD:
        if (!mfd_prog_remove(&d, n)) {
            *err = missing(n);
            ok = false;
            break;
        }
        for (size_t i = d.ngroups; i-- > 0;)
            if (strcmp(d.groups[i].program, n) == 0) mfd_group_remove_at(&d, i);
        mfd_place_remove(&d, n);
        *what = mt_fmt("unload %s", n);
        break;
    case MFE_RUN: {
        mfd_prog *p = mfd_prog_get(&d, n);
        if (!p) {
            *err = missing(n);
            ok = false;
            break;
        }
        p->run = req->on;
        *what = mt_fmt("%s %s", req->on ? "start" : "stop", n);
        break;
    }
    case MFE_RESET: {
        mfd_prog *p = mfd_prog_get(&d, n);
        if (!p) {
            *err = missing(n);
            ok = false;
            break;
        }
        if (p->lockstep) {
            /* A lockstep reset is the group's next epoch over the same members (rounds restart at 1 on all). */
            mfd_group *g = mfd_group_of(&d, n);
            if (!g) {
                *err = mt_fmt("%s: no group", n);
                ok = false;
                break;
            }
            mfd_gstate st = g->state;
            mfg_new_epoch(g, ml_clone(&g->members));
            g->state = st;
            *what = mt_fmt("reset %s: group epoch %u", n, g->epoch);
        } else {
            p->reset += 1;
            *what = mt_fmt("reset %s", n);
        }
        break;
    }
    case MFE_TASK: {
        mfd_group *g = mfd_group_of(&d, n);
        if (!g) {
            *err = mt_fmt("%s is not a lockstep program: its tasks are not changes (they run where asked)", n);
            ok = false;
            break;
        }
        if (g->state != MFD_RUNNING) {
            *err = mt_fmt("peer: group %s is paused (a member is missing): its tasks run on every member or none", g->name);
            ok = false;
            break;
        }
        g->cycle += 1;
        g->op = (mfd_op){MFD_OP_TASK, req->op, req->arg};
        *what = mt_fmt("task %s %u %u (group %s cycle %u)", n, req->op, req->arg, g->name, g->cycle);
        break;
    }
    case MFE_PLACE: ok = place(&d, req, cx, what, err); break;
    case MFE_REFORM: {
        mfd_group *g = mfd_group_named(&d, n);
        if (!g) {
            *err = no_group(n);
            ok = false;
            break;
        }
        const mfd_prog *p = mfd_prog_get(doc, g->program);
        if (!p) {
            *err = missing(g->program);
            ok = false;
            break;
        }
        ml_list up = cx->eligible_up(cx->ctx, &p->backends);
        mfd_group ng;
        ok = mfg_reform(g, &up, &ng, err);
        ml_free(&up);
        if (!ok) break;
        mfd_group_free(g);
        *g = ng;
        char *l = ml_text(&g->members);
        *what = mt_fmt("group %s reformed: epoch %u members %s", n, g->epoch, l);
        free(l);
        break;
    }
    case MFE_PIN: {
        mfd_prog *p = mfd_prog_get(&d, n);
        if (!p) {
            *err = missing(n);
            ok = false;
            break;
        }
        if (req->pin.some) {
            if (!req->pin.l.n) {
                *err = mt_strdup("sched pin: at least one member");
                ok = false;
                break;
            }
            for (size_t i = 0; i < req->pin.l.n && ok; i++)
                if (req->pin.l.v[i] >= cx->n) {
                    *err = mt_fmt("sched pin: system %u: the fleet has 0..%u", req->pin.l.v[i], cx->n - 1);
                    ok = false;
                }
            if (!ok) break;
            if (p->lockstep) {
                *err = mt_fmt("%s is the lockstep program: its group's members are `group %s reform`'s, not a pin", n, n);
                ok = false;
                break;
            }
            const char *a = p->needs.colocate_with;
            if (a && mfd_prog_get(doc, a)) {
                *err = mt_fmt("%s is colocated with %s: pin %s (what is colocated with it moves with it)", n, a, a);
                ok = false;
                break;
            }
        }
        ml_opt_free(&p->pin);
        p->pin = ml_opt_clone(&req->pin);
        if (req->pin.some) {
            char *l = ml_text(&req->pin.l);
            *what = mt_fmt("sched pin %s %s", n, l);
            free(l);
        } else *what = mt_fmt("sched unpin %s", n);
        break;
    }
    case MFE_SCHED_PAUSE:
        d.sched_paused = req->on;
        *what = mt_fmt("sched %s", req->on ? "pause" : "resume");
        break;
    case MFE_AUTOREFORM:
        d.autoreform = req->on;
        *what = mt_fmt("sched autoreform %s", req->on ? "on" : "off");
        break;
    case MFE_CORDON:
        if (req->k >= cx->n) {
            *err = mt_fmt("sched cordon: system %u: the fleet has 0..%u", req->k, cx->n - 1);
            ok = false;
            break;
        }
        if (req->on) d.cordon |= 1u << req->k;
        else d.cordon &= ~(1u << req->k);
        *what = mt_fmt("sched %scordon %u", req->on ? "" : "un", req->k);
        break;
    case MFE_ON_LOSS: {
        mfd_group *g = mfd_group_named(&d, n);
        if (!g) {
            *err = no_group(n);
            ok = false;
            break;
        }
        g->on_loss = req->on_loss;
        g->auto_ms = req->ms;
        char *after = req->ms > 0 ? mt_fmt(" after %u ms", req->ms) : mt_strdup("");
        *what = mt_fmt("group %s on_loss %s%s", n, req->on_loss == MFD_LOSS_PAUSE ? "pause" : "reform", after);
        free(after);
        break;
    }
    }
    if (!ok) {
        mfd_doc_free(&d);
        return false;
    }
    *out = d;
    return true;
}

/* The union of two sorted name arrays, sorted (a BTreeSet's iteration). */
static mt_strs union_sorted(char *const *a, size_t na, char *const *b, size_t nb) {
    mt_strs u = {0};
    size_t i = 0, j = 0;
    while (i < na || j < nb) {
        int c = i == na ? 1 : j == nb ? -1 : strcmp(a[i], b[j]);
        if (c <= 0) mt_push(&u, a[i]);
        else mt_push(&u, b[j]);
        if (c <= 0) i++;
        if (c >= 0) j++;
    }
    return u;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

mfd_doc mfe_revert(const mfd_doc *cur, const mfd_doc *before, const mfd_doc *after) {
    mfd_doc d = mfd_doc_clone(cur);
    mt_strs keys = union_sorted(before->pname, before->nprogs, after->pname, after->nprogs);
    for (size_t i = 0; i < keys.n; i++) {
        const mfd_prog *b = mfd_prog_get(before, keys.v[i]), *a = mfd_prog_get(after, keys.v[i]);
        if ((!b && !a) || (b && a && mfd_prog_eq(b, a))) continue;
        if (b) mfd_prog_put(&d, keys.v[i], mfd_prog_clone(b));
        else mfd_prog_remove(&d, keys.v[i]);
    }
    mt_strs_free(&keys);
    mt_strs names = {0};
    for (size_t i = 0; i < before->ngroups; i++)
        if (!mt_strs_has(&names, before->groups[i].name)) mt_push(&names, before->groups[i].name);
    for (size_t i = 0; i < after->ngroups; i++)
        if (!mt_strs_has(&names, after->groups[i].name)) mt_push(&names, after->groups[i].name);
    if (names.n > 1) qsort(names.v, names.n, sizeof *names.v, cmp_str);
    for (size_t i = 0; i < names.n; i++) {
        const mfd_group *b = mfd_group_named(before, names.v[i]), *a = mfd_group_named(after, names.v[i]);
        if ((!b && !a) || (b && a && mfd_group_eq(b, a))) continue;
        for (size_t j = d.ngroups; j-- > 0;)
            if (strcmp(d.groups[j].name, names.v[i]) == 0) mfd_group_remove_at(&d, j);
        if (b) mfd_group_push(&d, mfd_group_clone(b));
    }
    mt_strs_free(&names);
    keys = union_sorted(before->place_name, before->nplaces, after->place_name, after->nplaces);
    for (size_t i = 0; i < keys.n; i++) {
        const mt_strs *b = mfd_place_get(before, keys.v[i]), *a = mfd_place_get(after, keys.v[i]);
        if ((!b && !a) || (b && a && mt_strs_eq(b, a))) continue;
        if (b) mfd_place_put(&d, keys.v[i], mt_strs_clone(b));
        else mfd_place_remove(&d, keys.v[i]);
    }
    mt_strs_free(&keys);
    if (before->sched_paused != after->sched_paused) d.sched_paused = before->sched_paused;
    if (before->autoreform != after->autoreform) d.autoreform = before->autoreform;
    if (before->cordon != after->cordon) d.cordon = before->cordon;
    return d;
}
