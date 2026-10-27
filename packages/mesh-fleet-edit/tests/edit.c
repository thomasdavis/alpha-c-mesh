/* fleet.rs: changes_edit_the_document, a_lockstep_upgrade_must_run_on_every_member,
 * a_revert_undoes_only_its_change. */
#include "mesh/check.h"
#include "mesh/fleet_edit.h"
#include "mesh/text.h"
#include "mesh/abi/placement.h"

#include <stdlib.h>

static mfe_image image(const char *dir, const char *name, uint64_t hash, bool lockstep, const char *b0, const char *b1, bool autostart) {
    mfe_image im = {.dir = mt_strdup(dir), .name = mt_strdup(name), .hash = hash, .lockstep = lockstep, .autostart = autostart, .needs = msn_default()};
    mt_push(&im.backends, b0);
    if (b1) mt_push(&im.backends, b1);
    return im;
}

static bool cat(void *ctx, const char *dir, mfe_image *out, char **err) {
    (void)ctx;
    if (!strcmp(dir, "hello") || !strcmp(dir, "hello-b")) *out = image(dir, "hello", !strcmp(dir, "hello") ? 1 : 2, false, "cuda", NULL, true);
    else if (!strcmp(dir, "train") || !strcmp(dir, "train-b")) *out = image(dir, "train", !strcmp(dir, "train") ? 10 : 11, true, "cuda", "metal", true);
    else if (!strcmp(dir, "doom")) {
        *out = image(dir, "doom", 20, false, "cuda", NULL, false);
        out->placeable = true;
        out->place_cap = MESH_PLACE_CAP_SPLIT;
        out->place_fanout = 4;
    } else {
        *err = mt_fmt("manifest: %s: no such dir", dir);
        return false;
    }
    return true;
}

static ml_list up013(void *ctx, const mt_strs *b) { return (void)ctx, (void)b, ml_of((uint32_t[]){0, 1, 3}, 3); }

static mfe_ctx cx = {cat, up013, NULL, 4};

/* edit; the document, or NULL with *err. */
static mfd_doc *edit(const mfd_doc *d, const char *line, char **err) {
    mfe_req r;
    if (!mfe_parse(line, &r, err)) return NULL;
    mfd_doc *out = malloc(sizeof *out);
    char *what = NULL;
    bool ok = mfe_edit(d, &r, &cx, out, &what, err);
    mfe_req_free(&r);
    free(what);
    if (!ok) {
        free(out);
        return NULL;
    }
    return out;
}

static void drop(mfd_doc *d) {
    mfd_doc_free(d);
    free(d);
}

static bool fails(const mfd_doc *d, const char *line, const char *needle) {
    char *err = NULL;
    mfd_doc *x = edit(d, line, &err);
    if (x) {
        drop(x);
        return false;
    }
    bool ok = !needle || mt_contains(err, needle);
    if (!ok) fprintf(stderr, "error was: %s\n", err);
    free(err);
    return ok;
}

static void changes(void) {
    char *err = NULL;
    mfd_doc d0 = {0};
    mfe_req r;
    CHECK(mfe_parse("load hello", &r, &err));
    mfd_doc d1;
    char *what = NULL;
    CHECK(mfe_edit(&d0, &r, &cx, &d1, &what, &err));
    CHECK(mt_contains(what, "load hello") && mfd_prog_get(&d1, "hello")->run && mfd_prog_get(&d1, "hello")->hash == 1);
    free(what);
    mfe_req_free(&r);
    CHECK(fails(&d1, "load hello-b", "name exists"));
    CHECK(fails(&d1, "load nope", "manifest"));
    mfd_doc *d2 = edit(&d1, "upgrade hello hello-b", &err);
    mfd_prog *h = mfd_prog_get(d2, "hello");
    CHECK(h->hash == 2 && h->has_prev && !strcmp(h->prev_dir, "hello") && h->prev_hash == 1);
    mfd_doc *d3 = edit(d2, "rollback hello", &err);
    CHECK(mfd_prog_get(d3, "hello")->hash == 1 && !strcmp(mfd_prog_get(d3, "hello")->dir, "hello"));
    CHECK(fails(&d1, "rollback hello", "no previous"));
    mfd_doc *d4 = edit(d3, "load train", &err);
    mfd_group *g = mfd_group_of(d4, "train");
    CHECK(g && g->members.n == 3 && g->members.v[2] == 3 && g->epoch == 1 && g->state == MFD_RUNNING);
    mfd_doc *d5 = edit(d4, "upgrade train train-b", &err);
    CHECK(d5->groups[0].epoch == 1 && d5->groups[0].cycle == 1 && d5->groups[0].op.kind == MFD_OP_UPGRADE);
    mfd_doc *d6 = edit(d5, "task train 3 1000593162", &err);
    CHECK(d6->groups[0].cycle == 2 && d6->groups[0].op.kind == MFD_OP_TASK && d6->groups[0].op.op == 3 && d6->groups[0].op.arg == 1000593162);
    mfd_doc *d7 = edit(d6, "reset train", &err);
    CHECK(d7->groups[0].epoch == 2 && d7->groups[0].cycle == 0);
    mfd_doc p = mfd_doc_clone(d7);
    p.groups[0].state = MFD_PAUSED;
    CHECK(fails(&p, "task train 1 2", "paused"));
    CHECK(fails(d7, "task hello 1 2", NULL));
    mfd_doc *d8 = edit(&p, "group train reform", &err);
    CHECK(d8->groups[0].epoch == 3 && d8->groups[0].state == MFD_RUNNING);
    mfd_doc *d9 = edit(d8, "group train on_loss reform 5000", &err);
    CHECK(d9->groups[0].on_loss == MFD_LOSS_REFORM && d9->groups[0].auto_ms == 5000);
    mfd_doc *d10 = edit(d9, "load doom", &err);
    mfd_doc *d11 = edit(d10, "place doom split auto", &err);
    const mt_strs *pw = mfd_place_get(d11, "doom");
    CHECK(pw && pw->n == 2 && !strcmp(pw->v[0], "split") && !strcmp(pw->v[1], "auto"));
    CHECK(fails(d11, "place doom pin 9", NULL));
    CHECK(fails(d11, "place hello pin 1", "no `place`"));
    mfd_doc *d12 = edit(d11, "place doom local", &err);
    CHECK(!mfd_place_get(d12, "doom"));
    mfd_doc *d13 = edit(d11, "unload train", &err);
    CHECK(d13->ngroups == 0 && !mfd_prog_get(d13, "train"));
    mfd_doc *d14 = edit(d11, "unload doom", &err);
    CHECK(d14->nplaces == 0);
    CHECK(!mfe_parse("frobnicate x", &r, &err));
    CHECK_S(err, "not a change: \"frobnicate x\"");
    free(err);
    CHECK(!mfe_parse("task train x 1", &r, &err));
    free(err);
    /* The scheduler's changes. */
    mfd_doc *p1 = edit(d11, "sched pin hello 1,3", &err);
    mfd_prog *hp = mfd_prog_get(p1, "hello");
    CHECK(hp->pin.some && hp->pin.l.n == 2 && hp->pin.l.v[1] == 3);
    CHECK(fails(d11, "sched pin hello 7", NULL) && fails(d11, "sched pin train 1", "lockstep"));
    mfd_doc *p1b = edit(p1, "sched unpin hello", &err);
    CHECK(!mfd_prog_get(p1b, "hello")->pin.some);
    mfd_doc *p2 = edit(d11, "sched pause", &err);
    mfd_doc *p2b = edit(p2, "sched resume", &err);
    CHECK(p2->sched_paused && !p2b->sched_paused);
    mfd_doc *p3 = edit(d11, "sched autoreform on", &err);
    CHECK(p3->autoreform);
    mfd_doc *p4 = edit(d11, "sched cordon 3", &err);
    CHECK_U(p4->cordon, 8);
    CHECK(!mfe_parse("sched cordon 9", &r, &err));
    CHECK_S(err, "\"9\": a member");
    free(err);
    CHECK(!mfe_parse("sched pin x", &r, &err));
    free(err);
    CHECK(mfe_parse("unload hello --now", &r, &err) && r.kind == MFE_UNLOAD && !strcmp(r.name, "hello"));
    mfe_req_free(&r);
    CHECK(mfe_parse("task t 0x10 7", &r, &err) && r.op == 16 && r.arg == 7);
    mfe_req_free(&r);
    mfd_doc_free(&d1);
    mfd_doc_free(&p);
    drop(d2), drop(d3), drop(d4), drop(d5), drop(d6), drop(d7), drop(d8), drop(d9), drop(d10), drop(d11), drop(d12), drop(d13), drop(d14);
    drop(p1), drop(p1b), drop(p2), drop(p2b), drop(p3), drop(p4);
}

static bool cat2(void *ctx, const char *dir, mfe_image *out, char **err) {
    (void)ctx, (void)err;
    *out = image(dir, "train", strlen(dir), true, "cuda", strcmp(dir, "train-c") ? "metal" : NULL, true);
    return true;
}

/* System 3 is the Metal member. */
static ml_list up_metal(void *ctx, const mt_strs *b) {
    (void)ctx;
    ml_list v = ml_of((uint32_t[]){0, 1}, 2);
    if (mt_strs_has(b, "metal")) ml_push(&v, 3);
    return v;
}

static void lockstep_upgrade(void) {
    mfe_ctx c2 = {cat2, up_metal, NULL, 4};
    mfe_req r;
    char *err = NULL, *what = NULL;
    mfd_doc z = {0}, d, x;
    mfe_parse("load train", &r, &err);
    CHECK(mfe_edit(&z, &r, &c2, &d, &what, &err));
    free(what);
    mfe_req_free(&r);
    CHECK(d.groups[0].members.n == 3);
    mfe_parse("upgrade train train-c", &r, &err);
    CHECK(!mfe_edit(&d, &r, &c2, &x, &what, &err));
    CHECK(mt_starts(err, "unsupported") && mt_contains(err, "member(s) 3"));
    free(err);
    mfe_req uq;
    mfe_parse("upgrade train train-b", &uq, &err);
    CHECK(mfe_edit(&d, &uq, &c2, &x, &what, &err));
    free(what);
    mfd_doc_free(&x);
    mfe_req_free(&uq);
    /* Without system 3 in the group: allowed. */
    mfd_doc d2 = mfd_doc_clone(&d);
    ml_free(&d2.groups[0].members);
    d2.groups[0].members = ml_of((uint32_t[]){0, 1}, 2);
    CHECK(mfe_edit(&d2, &r, &c2, &x, &what, &err));
    free(what);
    mfd_doc_free(&x);
    mfe_req_free(&r);
    mfd_doc_free(&d);
    mfd_doc_free(&d2);
}

static mfd_doc base_doc(void) {
    mfd_doc d = {.version = 7, .term = 3};
    msn_needs n = msn_default();
    mfd_prog h = mfd_prog_new("hello", 0xabc, true, &n);
    h.reset = 2;
    mfd_prog_put(&d, "hello", h);
    mfd_prog t = mfd_prog_new("train-b", 0x123, true, &n);
    t.lockstep = true;
    mfd_prog_put(&d, "train", t);
    msn_free(&n);
    mfd_group_push(&d, (mfd_group){.name = mt_strdup("train"), .program = mt_strdup("train"), .epoch = 4, .cycle = 2, .members = ml_of((uint32_t[]){0, 2, 3}, 3),
                                   .state = MFD_RUNNING, .on_loss = MFD_LOSS_REFORM, .auto_ms = 5000});
    mt_strs w = {0};
    mt_push(&w, "split");
    mt_push(&w, "auto");
    mfd_place_put(&d, "doom", w);
    return d;
}

static void revert_only_its_change(void) {
    mfd_doc base = base_doc();
    mfd_doc bad = mfd_doc_clone(&base);
    mfd_prog_get(&bad, "hello")->hash = 0xbad;
    mfd_prog_put(&bad, "new", mfd_prog_clone(mfd_prog_get(&bad, "hello")));
    mfd_doc cur = mfd_doc_clone(&bad);
    cur.groups[0].state = MFD_PAUSED;
    mt_strs w = {0};
    mt_push(&w, "split");
    mfd_place_put(&cur, "hello", w);
    cur.version = 12;
    mfd_doc r = mfe_revert(&cur, &base, &bad);
    CHECK(r.nprogs == base.nprogs);
    for (size_t i = 0; i < r.nprogs && i < base.nprogs; i++) CHECK(!strcmp(r.pname[i], base.pname[i]) && mfd_prog_eq(&r.prog[i], &base.prog[i]));
    CHECK(r.groups[0].state == MFD_PAUSED && mfd_place_get(&r, "hello") && r.version == 12);
    mfd_doc r2 = mfe_revert(&bad, &base, &bad);
    CHECK(mfd_doc_eq(&r2, &base));
    mfd_doc_free(&r), mfd_doc_free(&r2), mfd_doc_free(&cur), mfd_doc_free(&bad), mfd_doc_free(&base);
}

int main(void) {
    changes();
    lockstep_upgrade();
    revert_only_its_change();
    return mesh_check_done();
}
