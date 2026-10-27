/* sched_tests.rs: placement_respects_constraints, same_inputs_same_decisions, reservations_are_admission,
 * a_member_down_is_replaced_after_the_grace, and node_shape_filters_members' placement. */
#include "fixture.h"
#include "mesh/check.h"

static void node_shape(void) {
    msm_member ms[4];
    fleet4(ms);
    mfd_doc d = doc_new();
    mesh_needs n = needs("cuda", "metal", NULL);
    n.warps = 16;
    n.shared = 48 << 10;
    mfd_prog_put(&d, "pt", prog_of(n));
    msm_perfs perf = {0};
    msw_weights w = msw_default();
    msp_input in = {&d, ms, 4, &perf, &w};
    mt_strs ch;
    msp_explains ex;
    mfd_doc out = msp_place(&in, &ch, &ex);
    const msp_explain *e = msp_explains_get(&ex, "pt");
    bool ok3 = true, rejected3 = false;
    for (size_t i = 0; i < e->nrows; i++) {
        if (e->rows[i].k < 3 && !e->rows[i].ok) ok3 = false;
        if (e->rows[i].k == 3 && !e->rows[i].ok) rejected3 = true;
    }
    CHECK(ok3 && rejected3);
    mfd_doc_free(&out), mfd_doc_free(&d);
    mt_strs_free(&ch);
    msp_explains_free(&ex);
    free_members(ms, 4);
}

static void constraints(void) {
    msm_member ms[4];
    fleet4(ms);
    mfd_doc d = doc_new();
    mesh_needs n;
    n = needs("metal", NULL, NULL), n.replicas = (mesh_replicas){false, 1}, mfd_prog_put(&d, "m", prog_of(n));
    n = needs("cuda", "metal", "wasm"), n.replicas = (mesh_replicas){false, 2}, mfd_prog_put(&d, "r2", prog_of(n));
    n = needs("wasm", NULL, NULL), n.replicas = (mesh_replicas){false, 1}, n.arch = mt_strdup("x86_64"), mfd_prog_put(&d, "x86", prog_of(n));
    n = needs("wasm", NULL, NULL), n.replicas = (mesh_replicas){false, 1};
    for (uint32_t k = 0; k < 4; k++) msn_terms_push(&n.avoid, &n.navoid, term(MESH_TERM_SYSTEM, NULL, k));
    mfd_prog_put(&d, "never", prog_of(n));
    n = needs("cuda", NULL, NULL), mfd_prog_put(&d, "all", prog_of(n));
    n = needs("wasm", NULL, NULL), n.replicas = (mesh_replicas){false, 1}, n.view_bytes = 8 << 20, mfd_prog_put(&d, "huge", prog_of(n));
    mfd_doc p = placed(ms, 4, &d);
    CHECK(at_is(&p, "m", (uint32_t[]){3}, 1));
    const mfd_prog *r2 = mfd_prog_get(&p, "r2");
    CHECK(r2->at.some && r2->at.l.n == 2 && r2->at.l.v[0] != r2->at.l.v[1]);
    CHECK(at_is(&p, "x86", (uint32_t[]){2}, 1));
    CHECK(at_is(&p, "never", NULL, 0));
    CHECK(mt_starts(mfd_prog_get(&p, "never")->why, "unplaced"));
    CHECK(!mfd_prog_get(&p, "all")->at.some);
    CHECK(at_is(&p, "huge", NULL, 0));
    CHECK(mt_contains(mfd_prog_get(&p, "huge")->why, "view heap"));
    /* A second pass changes nothing (a valid placement is kept). */
    msm_perfs perf = {0};
    msw_weights w = msw_default();
    msp_input in = {&p, ms, 4, &perf, &w};
    mt_strs ch;
    msp_explains ex;
    mfd_doc q = msp_place(&in, &ch, &ex);
    CHECK_U(ch.n, 0);
    CHECK(mfd_doc_eq(&q, &p));
    char *t = msp_explain_text(msp_explains_get(&ex, "m"));
    CHECK(mt_contains(t, "system 0 rejected backends metal"));
    free(t);
    mt_strs_free(&ch);
    msp_explains_free(&ex);
    mfd_doc_free(&q);
    /* A member where it failed is filtered (the leader remembers it), and the program moves off it. */
    ms_set(&ms[3].fails, "m", "load m: no image", true);
    q = placed(ms, 4, &p);
    CHECK(at_is(&q, "m", NULL, 0));
    CHECK(mt_contains(mfd_prog_get(&q, "m")->why, "failed here"));
    mfd_doc_free(&q), mfd_doc_free(&p), mfd_doc_free(&d);
    free_members(ms, 4);
}

static void same_inputs(void) {
    msm_member ms[4];
    for (uint32_t k = 0; k < 4; k++) {
        ms[k] = member(0, "cuda", "h");
        ms[k].id = k;
    }
    mfd_doc d = doc_new();
    mesh_needs n = needs("cuda", NULL, NULL);
    n.replicas = (mesh_replicas){false, 2};
    mfd_prog_put(&d, "a", prog_of(n));
    n = needs("cuda", NULL, NULL);
    n.replicas = (mesh_replicas){false, 1};
    mfd_prog_put(&d, "b", prog_of(n));
    mfd_doc p1 = placed(ms, 4, &d), p2 = placed(ms, 4, &d);
    CHECK(mfd_doc_eq(&p1, &p2));
    CHECK(at_is(&p1, "a", (uint32_t[]){0, 1}, 2));
    CHECK(at_is(&p1, "b", (uint32_t[]){2}, 1));
    mfd_doc dp = mfd_doc_clone(&d);
    mfd_prog_get(&dp, "b")->needs.pack = true;
    mfd_doc pp = placed(ms, 4, &dp);
    CHECK(at_is(&pp, "b", (uint32_t[]){0}, 1));
    mfd_doc_free(&p1), mfd_doc_free(&p2), mfd_doc_free(&dp), mfd_doc_free(&pp), mfd_doc_free(&d);
    free_members(ms, 4);
}

static void reservations(void) {
    msm_member ms[4];
    fleet4(ms);
    for (int i = 0; i < 4; i++) ms[i].cpus = 4;
    mfd_doc d = doc_new();
    const char *names[] = {"p0", "p1", "p2", "p3", "p4"};
    for (int i = 0; i < 5; i++) {
        mesh_needs n = needs("wasm", NULL, NULL);
        n.replicas = (mesh_replicas){false, 1};
        n.cpu = 2.0f;
        n.pack = true;
        mfd_prog_put(&d, names[i], prog_of(n));
    }
    mfd_doc p = placed(ms, 4, &d);
    uint32_t used = 0;
    for (int i = 0; i < 4; i++) {
        const mfd_prog *x = mfd_prog_get(&p, names[i]);
        CHECK(x->at.some && x->at.l.n == 1);
        CHECK(!(used >> x->at.l.v[0] & 1));
        used |= 1u << x->at.l.v[0];
    }
    CHECK(at_is(&p, "p4", NULL, 0));
    CHECK(mt_contains(mfd_prog_get(&p, "p4")->why, "reserved"));
    mfd_doc_free(&p), mfd_doc_free(&d);
    free_members(ms, 4);
    /* Views: two of 4 MiB do not fit one member's 7000 KiB heap run. */
    fleet4(ms);
    d = doc_new();
    for (int i = 0; i < 2; i++) {
        mesh_needs n = needs("wasm", NULL, NULL);
        n.replicas = (mesh_replicas){false, 1};
        n.view_bytes = 4 << 20;
        n.pack = true;
        mfd_prog_put(&d, i ? "v1" : "v0", prog_of(n));
    }
    p = placed(ms, 4, &d);
    CHECK(!ml_opt_eq(&mfd_prog_get(&p, "v0")->at, &mfd_prog_get(&p, "v1")->at));
    mfd_doc_free(&p), mfd_doc_free(&d);
    free_members(ms, 4);
}

static void member_down_replaced(void) {
    msm_member ms[4];
    fleet4(ms);
    mfd_doc d0 = doc_new();
    mesh_needs n = needs("cuda", NULL, NULL);
    n.replicas = (mesh_replicas){false, 1};
    msn_terms_push(&n.prefer, &n.nprefer, term(MESH_TERM_SYSTEM, NULL, 1));
    mfd_prog_put(&d0, "a", prog_of(n));
    mfd_doc d = placed(ms, 4, &d0);
    CHECK(at_is(&d, "a", (uint32_t[]){1}, 1));
    ms[1].up = false;
    ms[1].down_ms = 1000;
    mfd_doc q = placed(ms, 4, &d);
    CHECK(at_is(&q, "a", (uint32_t[]){1}, 1));
    mfd_doc_free(&q);
    ms[1].down_ms = 3000;
    mfd_doc p = placed(ms, 4, &d);
    const mfd_prog *a = mfd_prog_get(&p, "a");
    CHECK(a->at.some && a->at.l.n == 1 && a->at.l.v[0] != 1);
    CHECK(mt_contains(a->why, "replaced 1"));
    mfd_doc_free(&p), mfd_doc_free(&d), mfd_doc_free(&d0);
    free_members(ms, 4);
}

int main(void) {
    node_shape();
    constraints();
    same_inputs();
    reservations();
    member_down_replaced();
    return mesh_check_done();
}
