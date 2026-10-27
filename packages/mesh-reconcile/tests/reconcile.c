/* reconcile.rs a_cycle_is_hellod_only_with_its_image_and_task; and passes against a fake driver: loads,
 * unsupported programs, failures and the retry, a migration (load stopped, view, start), a lockstep
 * epoch and a cycle with its task, placement on the leader. */
#include "mesh/abi/reconcile.h"
#include "mesh/check.h"
#include "mesh/reconcile.h"
#include "mesh/text.h"

#include <stdlib.h>

static mfd_group group(mfd_op op) {
    return (mfd_group){.name = "train", .program = "train", .epoch = 2, .cycle = 3, .op = op, .members = ml_of((uint32_t[]){0, 1, 2}, 3), .state = MFD_RUNNING};
}

static void cycle_ready(void) {
    mfl_actual a = {.name = "train", .dir = "train", .hash = 0xa, .lockstep = true}, b = a;
    b.hash = 0xb;
    char *why = NULL;
    mfd_group up = group((mfd_op){MFD_OP_UPGRADE, 0, 0});
    CHECK(!mrc_cycle_ready(&up, 0xb, &a, 1, NULL, NULL, &why));
    CHECK(mt_contains(why, "runs 000000000000000a here, the group 000000000000000b") && mt_contains(why, "paused"));
    free(why);
    CHECK(mrc_cycle_ready(&up, 0xb, &b, 1, NULL, NULL, &why));
    CHECK(!mrc_cycle_ready(&up, 0xb, NULL, 0, NULL, NULL, &why) && mt_contains(why, "not loaded"));
    free(why);
    mfd_group t = group((mfd_op){MFD_OP_TASK, 3, 7});
    bool no = false, yes = true;
    CHECK(!mrc_cycle_ready(&t, 0xb, &b, 1, &no, "no result in 60s", &why) && mt_contains(why, "until the group's next epoch"));
    free(why);
    CHECK(!mrc_cycle_ready(&t, 0xb, &b, 1, NULL, NULL, &why) && mt_ends(why, "not run here"));
    free(why);
    CHECK(mrc_cycle_ready(&t, 0xb, &b, 1, &yes, NULL, &why));
    mfd_group rb = group((mfd_op){MFD_OP_ROLLBACK, 0, 0});
    CHECK(mrc_cycle_ready(&rb, 0xa, &a, 1, NULL, NULL, &why));
    ml_free(&up.members), ml_free(&t.members), ml_free(&rb.members);
}

/* ---- a fake driver */

typedef struct {
    char name[16], dir[16];
    uint64_t hash;
    bool run;
} prog;

typedef struct {
    prog p[16];
    int n;
    mt_strs calls; /* what the reconciler asked, in order */
    uint64_t applied;
    uint32_t last_status;
    char *last_reason;
    bool leader, want;
    mpk_hello hello;
    bool helloed;
    uint32_t words[3];
    const char *no_image; /* this program has no image here: unsupported */
    const char *fail_load;
} fake;

static prog *find(fake *f, const char *name) {
    for (int i = 0; i < f->n; i++)
        if (!strcmp(f->p[i].name, name)) return &f->p[i];
    return NULL;
}

static mfl_actual *inventory(void *ctx, size_t *n) {
    fake *f = ctx;
    mfl_actual *a = calloc((size_t)f->n + 1, sizeof *a);
    for (int i = 0; i < f->n; i++) a[i] = mfl_actual_new(f->p[i].name, f->p[i].dir, f->p[i].hash, false, 0, f->p[i].run);
    *n = (size_t)f->n;
    return a;
}

typedef struct {
    char name[16], dir[16];
    uint64_t hash;
} src_t;

static bool source(void *ctx, const char *name, const char *dir, uint64_t hash, void **src, char **err) {
    fake *f = ctx;
    if (f->no_image && !strcmp(name, f->no_image)) {
        *err = mt_fmt("unsupported: no image of %s for this member", name);
        return false;
    }
    src_t *s = calloc(1, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name), snprintf(s->dir, sizeof s->dir, "%s", dir);
    s->hash = hash;
    *src = s;
    return true;
}

static void src_free(void *ctx, void *src) { (void)ctx, free(src); }

static bool load(void *ctx, void *src, bool run, char **text, char **err) {
    fake *f = ctx;
    src_t *s = src;
    if (f->fail_load && !strcmp(s->name, f->fail_load)) {
        *err = mt_strdup("jit: boom");
        return false;
    }
    prog *p = &f->p[f->n++];
    snprintf(p->name, sizeof p->name, "%s", s->name), snprintf(p->dir, sizeof p->dir, "%s", s->dir);
    p->hash = s->hash, p->run = run;
    mt_push_own(&f->calls, mt_fmt("load %s %s", s->name, run ? "running" : "stopped"));
    *text = mt_strdup("slot 1");
    return true;
}

static bool upgrade(void *ctx, const char *name, void *src, char **text, char **err) {
    (void)err;
    fake *f = ctx;
    find(f, name)->hash = ((src_t *)src)->hash;
    mt_push_own(&f->calls, mt_fmt("upgrade %s", name));
    *text = mt_strdup("jit 1.0 ms");
    return true;
}

static bool unload(void *ctx, const char *name, char **text, char **err) {
    (void)err;
    fake *f = ctx;
    prog *p = find(f, name);
    *p = f->p[--f->n];
    mt_push_own(&f->calls, mt_fmt("unload %s", name));
    *text = mt_strdup("");
    return true;
}

static bool simple(fake *f, const char *what, const char *name, char **text) {
    mt_push_own(&f->calls, mt_fmt("%s %s", what, name));
    *text = mt_strdup("ok");
    return true;
}

static bool rollback(void *ctx, const char *name, char **text, char **err) { return (void)err, simple(ctx, "rollback", name, text); }
static bool reset(void *ctx, const char *name, char **text, char **err) { return (void)err, simple(ctx, "reset", name, text); }

static bool prog_run(void *ctx, const char *name, bool on, char **text, char **err) {
    (void)err;
    fake *f = ctx;
    find(f, name)->run = on;
    return simple(f, on ? "start" : "stop", name, text);
}

static bool task(void *ctx, const char *name, uint32_t op, uint32_t arg, uint32_t *result, char **err) {
    (void)err;
    fake *f = ctx;
    mt_push_own(&f->calls, mt_fmt("task %s %u %u", name, op, arg));
    *result = op + arg;
    return true;
}

static bool place(void *ctx, const char *name, const mt_strs *words, char **text, char **err) {
    (void)err;
    fake *f = ctx;
    char *j = mt_join(words, " ");
    mt_push_own(&f->calls, mt_fmt("place %s %s", name, j));
    free(j);
    *text = mt_strdup("done");
    return true;
}

static bool fetch_view(void *ctx, const char *name, uint64_t hash, uint32_t from, uint32_t *abi, mpw_bytes *b, char **err) {
    (void)hash, (void)err;
    fake *f = ctx;
    mt_push_own(&f->calls, mt_fmt("fetch view %s from %u", name, from));
    *abi = 1;
    mpw_put(b, "view", 4);
    return true;
}

static bool view_put(void *ctx, const char *name, uint32_t abi, const mpw_bytes *b, char **text, char **err) {
    (void)err;
    fake *f = ctx;
    mt_push_own(&f->calls, mt_fmt("view put %s %u %zu", name, abi, b->n));
    *text = mt_strdup("");
    return true;
}

static void quiesce(void *ctx, bool mirror, bool member) {
    fake *f = ctx;
    f->want = false;
    mt_push_own(&f->calls, mt_fmt("quiesce mirror %d member %d", mirror, member));
}

static bool want_set(void *ctx) { return ((fake *)ctx)->want; }

static void group_words(void *ctx, uint32_t rank, uint32_t n, uint32_t epoch) {
    fake *f = ctx;
    f->words[0] = rank, f->words[1] = n, f->words[2] = epoch;
}

static void ready(void *ctx, const mpk_hello *spec) {
    fake *f = ctx;
    f->hello = *spec;
    f->helloed = f->want = true;
}

static bool is_leader(void *ctx) { return ((fake *)ctx)->leader; }

static void status(void *ctx, uint64_t applied, uint32_t st, const char *reason, const mst_outcome *m, size_t nm) {
    (void)m, (void)nm;
    fake *f = ctx;
    f->applied = applied, f->last_status = st;
    mt_set(&f->last_reason, reason);
}

static void fails(void *ctx, char *const *n, const uint64_t *h, const bool *u, char *const *w, size_t k) { (void)ctx, (void)n, (void)h, (void)u, (void)w, (void)k; }
static uint64_t applied(void *ctx) { return ((fake *)ctx)->applied; }
static void logline(void *ctx, bool error, const char *line) { (void)ctx, (void)error, (void)line; }
static void sleep_ms(void *ctx, uint32_t ms) { (void)ctx, (void)ms; }

static mrc_ops ops(fake *f, uint32_t me) {
    return (mrc_ops){f, me, inventory, source, src_free, load, upgrade, unload, rollback, prog_run, reset, task, place, fetch_view, view_put,
                     quiesce, want_set, group_words, ready, is_leader, status, fails, applied, logline, sleep_ms};
}

static mfd_doc doc(void) {
    mfd_doc d = {.version = 3, .term = 1};
    msn_needs n = msn_default();
    mfd_prog_put(&d, "hello", mfd_prog_new("hello", 0xabc, true, &n));
    mfd_prog_put(&d, "tick", mfd_prog_new("tick", 0x123, false, &n));
    msn_free(&n);
    return d;
}

static bool called(fake *f, const char *what) { return mt_strs_has(&f->calls, what); }

static void passes(void) {
    fake f = {0};
    mrc_ops o = ops(&f, 0);
    mrc_rec *r = mrc_new(&o);
    mfd_doc d = doc();
    CHECK(!mrc_document(r, &d));
    CHECK(called(&f, "load hello running") && called(&f, "load tick stopped") && f.last_status == MESH_RS_SYNC && f.applied == 3);
    CHECK(mt_starts(f.last_reason, "ok: load hello (slot 1), load tick (slot 1)"));
    /* In sync: nothing to do. */
    mt_strs_free(&f.calls);
    CHECK(!mrc_document(r, &d) && f.calls.n == 0 && !strcmp(f.last_reason, "ok"));
    /* An upgrade by hash, a program placed elsewhere unloaded, and an unsupported one reported, not failed. */
    mfd_doc e = mfd_doc_clone(&d);
    e.version = 4;
    mfd_prog_get(&e, "hello")->hash = 0xdef;
    mfd_prog_get(&e, "tick")->at = ml_some((uint32_t[]){2}, 1);
    msn_needs n = msn_default();
    mfd_prog_put(&e, "metal-only", mfd_prog_new("metal-only", 7, true, &n));
    msn_free(&n);
    f.no_image = "metal-only";
    CHECK(!mrc_document(r, &e));
    CHECK(called(&f, "upgrade hello") && called(&f, "unload tick") && f.last_status == MESH_RS_SYNC && mt_contains(f.last_reason, "unsupported here: metal-only"));
    /* A failure: reported, skipped at that hash, and tried again on the retry. */
    mfd_doc g = mfd_doc_clone(&e);
    g.version = 5;
    n = msn_default();
    mfd_prog_put(&g, "flaky", mfd_prog_new("flaky", 9, true, &n));
    msn_free(&n);
    f.fail_load = "flaky";
    CHECK(mrc_document(r, &g) && f.last_status == MESH_RS_FAILED && mt_contains(f.last_reason, "load flaky: jit: boom"));
    mt_strs_free(&f.calls);
    CHECK(!mrc_document(r, &g)); /* skipped (same version, same hash): nothing failed this pass */
    CHECK(!called(&f, "load flaky running"));
    f.fail_load = NULL;
    CHECK(!mrc_retry(r) && called(&f, "load flaky running") && f.last_status == MESH_RS_SYNC);
    /* Placement: the leader runs the requests; one dropped from the document goes back to local. */
    f.leader = true;
    mfd_doc h = mfd_doc_clone(&g);
    h.version = 6;
    mt_strs w = {0};
    mt_push(&w, "pin");
    mt_push(&w, "1");
    mfd_place_put(&h, "hello", w);
    mt_strs_free(&f.calls);
    CHECK(!mrc_document(r, &h) && called(&f, "place hello pin 1"));
    mfd_place_remove(&h, "hello");
    h.version = 7;
    mt_strs_free(&f.calls);
    CHECK(!mrc_document(r, &h) && called(&f, "place hello local"));
    mfd_doc_free(&d), mfd_doc_free(&e), mfd_doc_free(&g), mfd_doc_free(&h);
    mrc_free(r);
    mt_strs_free(&f.calls);
    free(f.last_reason);
}

static void migration(void) {
    fake f = {0};
    mrc_ops o = ops(&f, 1);
    mrc_rec *r = mrc_new(&o);
    mfd_doc d = {.version = 8, .term = 1};
    msn_needs n = msn_default();
    mt_push(&n.feeds, "pipehash");
    mfd_prog src = mfd_prog_new("pipesrc", 1, true, &n);
    msn_free(&n);
    n = msn_default();
    mfd_prog hash = mfd_prog_new("pipehash", 2, true, &n);
    msn_free(&n);
    src.at = ml_some((uint32_t[]){1}, 1), hash.at = ml_some((uint32_t[]){1}, 1);
    src.has_from = hash.has_from = src.has_to = hash.has_to = true;
    src.from = hash.from = 2, src.to = hash.to = 1, src.since = hash.since = 8;
    mfd_prog_put(&d, "pipesrc", src);
    mfd_prog_put(&d, "pipehash", hash);
    CHECK(!mrc_document(r, &d));
    /* Loaded stopped, views producers first, started consumers first. */
    long ls = mt_strs_find(&f.calls, "load pipesrc stopped"), lh = mt_strs_find(&f.calls, "load pipehash stopped");
    long vs = mt_strs_find(&f.calls, "fetch view pipesrc from 2"), vh = mt_strs_find(&f.calls, "fetch view pipehash from 2");
    long ss = mt_strs_find(&f.calls, "start pipesrc"), sh = mt_strs_find(&f.calls, "start pipehash");
    CHECK(ls >= 0 && lh > ls && vs > lh && vh > vs && sh > vh && ss > sh);
    CHECK(called(&f, "view put pipehash 1 4") && mt_contains(f.last_reason, "migrate pipehash,pipesrc from 2: view 8 B"));
    mfd_doc_free(&d);
    mrc_free(r);
    mt_strs_free(&f.calls);
    free(f.last_reason);
}

static void lockstep(void) {
    fake f = {0};
    mrc_ops o = ops(&f, 2);
    mrc_rec *r = mrc_new(&o);
    mfd_doc d = {.version = 2, .term = 1};
    msn_needs n = msn_default();
    mfd_prog t = mfd_prog_new("train", 0x77, true, &n);
    t.lockstep = true;
    mfd_prog_put(&d, "train", t);
    msn_free(&n);
    mfd_group_push(&d, (mfd_group){.name = mt_strdup("train"), .program = mt_strdup("train"), .epoch = 1, .members = ml_of((uint32_t[]){0, 2}, 2), .state = MFD_RUNNING});
    CHECK(!mrc_document(r, &d));
    CHECK(called(&f, "quiesce mirror 0 member 1") && called(&f, "load train running") && called(&f, "reset train"));
    CHECK(f.words[0] == 1 && f.words[1] == 2 && f.words[2] == 1 && f.helloed && f.hello.epoch == 1 && f.hello.cycle == 0 && f.hello.mask == 5 && f.hello.hash == 0x77);
    CHECK(f.hello.order[0] == 0 && f.hello.order[1] == 2 && f.hello.order[2] == 0xff);
    /* A task cycle: quiesced with rounds mirrored, the task run, HELLOed. */
    mfd_doc c = mfd_doc_clone(&d);
    c.version = 3;
    c.groups[0].cycle = 1;
    c.groups[0].op = (mfd_op){MFD_OP_TASK, 3, 4};
    f.helloed = false;
    mt_strs_free(&f.calls);
    CHECK(!mrc_document(r, &c));
    CHECK(called(&f, "quiesce mirror 1 member 1") && called(&f, "task train 3 4") && f.helloed && f.hello.cycle == 1);
    CHECK(mt_contains(f.last_reason, "task train -> 7 (cycle 1)"));
    /* Paused: unfed, reported. */
    mfd_doc p = mfd_doc_clone(&c);
    p.version = 4;
    p.groups[0].state = MFD_PAUSED;
    f.want = true;
    mt_strs_free(&f.calls);
    CHECK(!mrc_document(r, &p) && called(&f, "quiesce mirror 0 member 1") && mt_contains(f.last_reason, "group train epoch 1: paused"));
    /* The lockstep program leaves the document: its group goes too. */
    mfd_doc z = {.version = 5, .term = 1};
    f.want = false;
    mt_strs_free(&f.calls);
    CHECK(!mrc_document(r, &z));
    mfd_doc_free(&d), mfd_doc_free(&c), mfd_doc_free(&p), mfd_doc_free(&z);
    mrc_free(r);
    mt_strs_free(&f.calls);
    free(f.last_reason);
}

int main(void) {
    cycle_ready();
    passes();
    migration();
    lockstep();
    return mesh_check_done();
}
