/* sched_tests.rs: colocation_and_pins, hysteresis_and_budget, owner_policy_has_hysteresis,
 * groups_are_proposed_not_moved, a_new_leader_takes_over_a_migration,
 * the_moves_verdict_is_its_programs_not_the_whole_pass, a_pin_never_splits_a_colocated_pair (the
 * scheduler's half), and the 10-minute simulated fleet. */
#include "fixture.h"
#include "mesh/check.h"
#include "mesh/sched_tracker.h"

static msp_input input(const mfd_doc *d, const msm_member *ms, const msm_perfs *perf, const msw_weights *w) { return (msp_input){d, ms, 4, perf, w}; }

static mfd_doc pipe_doc(bool other) {
    mfd_doc d = doc_new();
    msn_needs n = needs("cuda", "metal", NULL);
    n.replicas = (msn_replicas){false, 1};
    msn_terms_push(&n.prefer, term(MSN_SYSTEM, NULL, 2));
    mt_push(&n.feeds, "pipesink-w");
    mfd_prog_put(&d, "pipehash", prog_of(n));
    n = needs("wasm", NULL, NULL);
    n.colocate_with = mt_strdup("pipehash");
    mt_push(&n.feeds, "pipehash");
    if (!other) mt_push(&n.feeds, "pipesink-w");
    mfd_prog_put(&d, "pipesrc", prog_of(n));
    n = needs("wasm", NULL, NULL);
    n.colocate_with = mt_strdup("pipehash");
    mfd_prog_put(&d, "pipesink-w", prog_of(n));
    if (other) {
        n = needs("wasm", NULL, NULL);
        n.replicas = (msn_replicas){false, 1};
        mfd_prog_put(&d, "other", prog_of(n));
    }
    return d;
}

static void colocation_and_pins(void) {
    msm_member ms[4];
    fleet4(ms);
    mfd_doc d = pipe_doc(false);
    msn_needs n = needs("wasm", NULL, NULL);
    n.colocate_with = mt_strdup("absent");
    mfd_prog_put(&d, "lone", prog_of(n));
    mfd_doc p = placed(ms, 4, &d);
    CHECK(at_is(&p, "pipehash", (uint32_t[]){2}, 1) && at_is(&p, "pipesrc", (uint32_t[]){2}, 1) && at_is(&p, "pipesink-w", (uint32_t[]){2}, 1));
    CHECK(!mfd_prog_get(&p, "lone")->at.some);
    mt_strs u = mst_unit(&p, "pipehash", 2);
    CHECK(u.n == 3 && !strcmp(u.v[0], "pipesrc") && !strcmp(u.v[1], "pipehash") && !strcmp(u.v[2], "pipesink-w"));
    mt_strs_free(&u);
    /* A pin of a running stateful program is a migration (the leader's); restart programs move at once. */
    mfd_doc dp = mfd_doc_clone(&p);
    mfd_prog_get(&dp, "pipehash")->pin = ml_some((uint32_t[]){0}, 1);
    mfd_doc q = placed(ms, 4, &dp);
    CHECK(at_is(&q, "pipehash", (uint32_t[]){2}, 1));
    mst_tracker *t = mst_new();
    msm_perfs perf = {0};
    msw_weights w = msw_default();
    msp_input in = input(&q, ms, &perf, &w);
    mst_move mv;
    CHECK(mst_step(t, &in, 0, false, &mv));
    CHECK(mv.forced && mv.from == 2 && mv.to == 0 && mv.programs.n == 3);
    mfd_doc s = mst_start_move(&q, &mv, q.version + 1);
    size_t from2 = 0;
    for (size_t i = 0; i < s.nprogs; i++) from2 += s.prog[i].has_from && s.prog[i].from == 2;
    CHECK(from2 == 3 && at_is(&s, "pipesrc", (uint32_t[]){0}, 1));
    mfd_doc e = mst_end_move(&s, &mv, true, "done");
    bool none_from = true;
    for (size_t i = 0; i < e.nprogs; i++) none_from &= !e.prog[i].has_from;
    CHECK(none_from && at_is(&e, "pipehash", (uint32_t[]){0}, 1));
    mfd_doc back = mst_end_move(&s, &mv, false, "reverted");
    CHECK(at_is(&back, "pipehash", (uint32_t[]){2}, 1));
    mfd_prog_get(&dp, "pipehash")->needs.migrate = MSN_RESTART;
    mfd_doc r = placed(ms, 4, &dp);
    CHECK(at_is(&r, "pipehash", (uint32_t[]){0}, 1));
    mst_move_free(&mv);
    mst_free(t);
    mfd_doc_free(&d), mfd_doc_free(&p), mfd_doc_free(&dp), mfd_doc_free(&q), mfd_doc_free(&s), mfd_doc_free(&e), mfd_doc_free(&back), mfd_doc_free(&r);
    free_members(ms, 4);
}

static void hysteresis_and_budget(void) {
    msm_member ms[4];
    fleet4(ms);
    msw_weights w = msw_default();
    w.hold_ms = 3000, w.dwell_ms = 10000, w.per_min = 1;
    mfd_doc d0 = doc_new();
    for (int i = 0; i < 2; i++) {
        msn_needs n = needs("wasm", NULL, NULL);
        n.replicas = (msn_replicas){false, 1};
        msn_terms_push(&n.prefer, term(MSN_SYSTEM, NULL, 0));
        mfd_prog_put(&d0, i ? "b" : "a", prog_of(n));
    }
    mfd_doc d = placed(ms, 4, &d0);
    CHECK(at_is(&d, "a", (uint32_t[]){0}, 1));
    msm_perfs perf = {0};
    mst_tracker *t = mst_new();
    mst_move m;
    for (uint64_t now = 0; now < 10000; now += 1000) {
        msp_input in = input(&d, ms, &perf, &w);
        CHECK(!mst_step(t, &in, now, false, &m));
    }
    ms[0].cpu_util = 1.0f;
    ms[0].psi_cpu = mof32_some(0.5f);
    uint64_t at = 0;
    bool got = false;
    for (uint64_t now = 10000; now < 20000 && !got; now += 500) {
        msp_input in = input(&d, ms, &perf, &w);
        if (mst_step(t, &in, now, false, &m)) got = true, at = now;
    }
    CHECK(got && at >= 13000 && at <= 14000);
    CHECK_U(m.from, 0);
    mst_moved(t, &m, at);
    mfd_doc s = mst_start_move(&d, &m, d.version + 1);
    mfd_doc_free(&d);
    d = mst_end_move(&s, &m, true, "done");
    mfd_doc_free(&s);
    mst_move_free(&m);
    bool second = false;
    for (uint64_t now = at; now < at + 30000 && !second; now += 500) {
        msp_input in = input(&d, ms, &perf, &w);
        if (mst_step(t, &in, now, false, &m)) second = true, mst_move_free(&m);
    }
    CHECK(!second);
    bool budget = false;
    for (size_t i = 0; i < mst_notes(t)->n; i++) budget |= mt_contains(mst_notes(t)->v[i], "budget");
    CHECK(budget);
    msp_input in = input(&d, ms, &perf, &w);
    CHECK(!mst_step(t, &in, at + 70000, true, &m));
    CHECK(mst_step(t, &in, at + 70000, false, &m));
    mst_move_free(&m);
    mst_free(t);
    mfd_doc_free(&d), mfd_doc_free(&d0);
    free_members(ms, 4);
}

static void owner_policy(void) {
    msm_member ms[4];
    fleet4(ms);
    msw_weights w = msw_default();
    w.owner_ms = 3000;
    mst_tracker *t = mst_new();
    ms[0].gpu_share = mof32_some(0.2f);
    mst_owner(t, ms, 4, 0, &w);
    CHECK(!ms[0].busy);
    mst_owner(t, ms, 4, 2000, &w);
    ms[0].gpu_share = mof32_some(0.5f);
    mst_owner(t, ms, 4, 2500, &w);
    ms[0].gpu_share = mof32_some(0.2f);
    mst_owner(t, ms, 4, 3000, &w);
    CHECK(!ms[0].busy);
    mst_owner(t, ms, 4, 6100, &w);
    CHECK(ms[0].busy != NULL);
    CHECK_S(ms[0].busy, "other tenants load it (share 0.20, PSI cpu 0.010)");
    ms[0].gpu_share = mof32_some(0.35f);
    mst_owner(t, ms, 4, 7000, &w);
    mst_owner(t, ms, 4, 20000, &w);
    CHECK(ms[0].busy != NULL);
    ms[0].gpu_share = mof32_some(0.6f);
    mst_owner(t, ms, 4, 21000, &w);
    mst_owner(t, ms, 4, 24100, &w);
    CHECK(!ms[0].busy);
    MSM_SET(ms[3].gpu, "wasm");
    ms[3].gpu_share = mof32_some(0.1f);
    mst_owner(t, ms, 4, 0, &w);
    mst_owner(t, ms, 4, 5000, &w);
    mst_owner(t, ms, 4, 10000, &w);
    CHECK(!ms[3].busy);
    mst_free(t);
    free_members(ms, 4);
}

static void groups_proposed(void) {
    msm_member ms[4];
    fleet4(ms);
    mfd_doc d = doc_new();
    msn_needs n = needs("cuda", "metal", "wasm");
    n.lockstep = true;
    n.group_size = 3;
    msn_terms_push(&n.group_members, term(MSN_BACKEND, "cuda", 0));
    mfd_prog_put(&d, "train", prog_of(n));
    mfd_group_push(&d, (mfd_group){.name = mt_strdup("train"), .program = mt_strdup("train"), .epoch = 1, .members = ml_of((uint32_t[]){0, 1, 2}, 3),
                                   .state = MFD_RUNNING, .on_loss = MFD_LOSS_PAUSE});
    msm_perfs perf = {0};
    msw_weights w = msw_default();
    w.hold_ms = 2000;
    msp_input in = input(&d, ms, &perf, &w);
    ml_list ideal;
    char *why;
    CHECK(msp_group_choice(&in, &d.groups[0], &ideal, &why));
    CHECK(ideal.n == 3 && ideal.v[0] == 0 && ideal.v[2] == 2);
    ml_free(&ideal);
    free(why);
    mst_tracker *t = mst_new();
    char *g;
    ml_list m;
    CHECK(!mst_group_step(t, &in, 0, &g, &m, &why));
    ms[1].up = false;
    CHECK(!mst_group_step(t, &in, 1000, &g, &m, &why));
    CHECK(mst_group_step(t, &in, 3500, &g, &m, &why));
    CHECK(m.n == 2 && m.v[0] == 0 && m.v[1] == 2);
    free(g), free(why), ml_free(&m);
    mt_strs ch;
    msp_explains ex;
    mfd_doc q = msp_place(&in, &ch, &ex);
    CHECK(ch.n == 0 && !mfd_prog_get(&q, "train")->at.some);
    mt_strs_free(&ch), msp_explains_free(&ex), mfd_doc_free(&q);
    mst_move mv;
    CHECK(!mst_step(t, &in, 10000, false, &mv));
    ms[1].up = true;
    msn_terms_free(&mfd_prog_get(&d, "train")->needs.group_members);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) ms[i].rtt_us[j] = mof32_some(250.0f);
        ms[i].rtt_us[3] = mof32_some(1900.0f);
    }
    for (int j = 0; j < 4; j++) ms[3].rtt_us[j] = mof32_some(1900.0f);
    CHECK(msp_group_choice(&in, &d.groups[0], &ideal, &why));
    CHECK(ideal.n == 3 && ideal.v[0] == 0 && ideal.v[1] == 1 && ideal.v[2] == 2);
    ml_free(&ideal), free(why);
    mst_free(t);
    mfd_doc_free(&d);
    free_members(ms, 4);
}

/* The colocated unit pipesrc + pipehash + pipesink-w on system 2, moving to 0 (state), as version 8. */
static mfd_doc moving(mst_move *mv) {
    msm_member ms[4];
    fleet4(ms);
    mfd_doc d = pipe_doc(true);
    mfd_doc p = placed(ms, 4, &d);
    *mv = (mst_move){mst_unit(&p, "pipehash", 2), 2, 0, true, mt_strdup("test"), false};
    mfd_doc s = mst_start_move(&p, mv, 8);
    s.version = 8;
    mfd_doc_free(&d), mfd_doc_free(&p);
    free_members(ms, 4);
    return s;
}

static mst_outcome out_of(const char *name, uint64_t v, bool ok) { return (mst_outcome){(char *)name, v, ok, ok ? 2500 : 0}; }

static void new_leader_takes_over(void) {
    mst_move mv;
    mfd_doc s0 = moving(&mv);
    size_t c = 0;
    for (size_t i = 0; i < s0.nprogs; i++) c += s0.prog[i].has_from && s0.prog[i].from == 2 && s0.prog[i].has_to && s0.prog[i].to == 0 && s0.prog[i].since == 8;
    CHECK_U(c, 3);
    /* The document round-trips the move. */
    char *text = mfd_encode(&s0), *err = NULL;
    mfd_doc s;
    CHECK(mfd_decode(text, &s, &err));
    free(text);
    mst_resumed r = mst_resume_moves(&s);
    CHECK_U(r.n, 1);
    const mst_move *m = &r.moves[0];
    CHECK(m->from == 2 && m->to == 0 && r.since[0] == 8 && m->state && m->programs.n == 3);
    for (size_t i = 0; i < mv.programs.n; i++) CHECK(mt_strs_has(&m->programs, mv.programs.v[i]));
    mst_outcome ok[] = {out_of("pipesrc", 9, true), out_of("pipehash", 9, true), out_of("pipesink-w", 9, true)};
    bool good;
    char *why;
    double gap;
    CHECK(mst_mig_verdict(m, 8, true, ok, 3, "", 100, 90000, &good, &why, &gap) && good && gap == 2.5);
    free(why);
    mfd_doc e = mst_end_move(&s, m, true, "done");
    bool clear = true;
    for (size_t i = 0; i < e.nprogs; i++) clear &= !e.prog[i].has_from && !e.prog[i].has_to;
    CHECK(clear && at_is(&e, "pipehash", (uint32_t[]){0}, 1));
    mst_resumed none = mst_resume_moves(&e);
    CHECK_U(none.n, 0);
    mst_resumed_free(&none);
    mst_outcome old[] = {out_of("pipesrc", 5, false), out_of("pipehash", 5, false), out_of("pipesink-w", 5, false)};
    CHECK(!mst_mig_verdict(m, 8, true, old, 3, "", 100, 90000, &good, &why, &gap));
    CHECK(mst_mig_verdict(m, 8, true, old, 3, "", 90001, 90000, &good, &why, &gap) && !good);
    free(why);
    CHECK(mst_mig_verdict(m, 8, false, ok, 3, "", 100, 90000, &good, &why, &gap) && !good);
    free(why);
    mfd_doc back = mst_end_move(&s, m, false, "back");
    CHECK(at_is(&back, "pipehash", (uint32_t[]){2}, 1) && !mfd_prog_get(&back, "pipehash")->has_from);
    mfd_doc_free(&back), mfd_doc_free(&e), mfd_doc_free(&s), mfd_doc_free(&s0);
    mst_resumed_free(&r);
    mst_move_free(&mv);
}

static void verdict_is_its_programs(void) {
    mst_move mv;
    mfd_doc s = moving(&mv);
    mst_outcome o[] = {out_of("pipesrc", 8, true), out_of("pipehash", 8, true), out_of("pipesink-w", 8, true), out_of("other", 8, false)};
    bool good;
    char *why;
    double gap;
    CHECK(mst_mig_verdict(&mv, 8, true, o, 4, "load other: jit: boom", 100, 90000, &good, &why, &gap) && good);
    free(why);
    o[1].ok = false;
    CHECK(mst_mig_verdict(&mv, 8, true, o, 4, "migrate pipehash from system 2: no view", 100, 90000, &good, &why, &gap) && !good);
    CHECK(mt_contains(why, "pipehash") && !mt_contains(why, "pipesrc,"));
    free(why);
    CHECK(!mst_mig_verdict(&mv, 8, true, o, 2, "", 100, 90000, &good, &why, &gap));
    mfd_doc_free(&s);
    mst_move_free(&mv);
}

static void pin_never_splits(void) {
    msm_member ms[4];
    fleet4(ms);
    mst_move mv;
    mfd_doc s = moving(&mv);
    mfd_doc ended = mst_end_move(&s, &mv, true, "done");
    mfd_doc d = placed(ms, 4, &ended);
    CHECK(at_is(&d, "pipehash", (uint32_t[]){0}, 1) && at_is(&d, "pipesrc", (uint32_t[]){0}, 1));
    mfd_prog_get(&d, "pipesrc")->pin = ml_some((uint32_t[]){1}, 1);
    mfd_doc p = placed(ms, 4, &d);
    CHECK(at_is(&p, "pipesrc", (uint32_t[]){0}, 1));
    mst_tracker *t = mst_new();
    msm_perfs perf = {0};
    msw_weights w = msw_default();
    msp_input in = input(&p, ms, &perf, &w);
    mst_move m;
    CHECK(!mst_step(t, &in, 0, false, &m));
    mst_free(t);
    mfd_doc_free(&p), mfd_doc_free(&d), mfd_doc_free(&ended), mfd_doc_free(&s);
    mst_move_free(&mv);
    free_members(ms, 4);
}

/* A simulated fleet over 10 minutes: members leave and return, the load oscillates on one member; the
 * placement stays valid at every step and programs do not flap. */
static void simulation(void) {
    msw_weights w = msw_default();
    w.hold_ms = 6000, w.dwell_ms = 20000, w.per_min = 3, w.replace_ms = 3000;
    msm_member ms[4];
    fleet4(ms);
    mfd_doc d = doc_new();
    msn_needs n;
    n = needs("cuda", "metal", "wasm"), n.replicas = (msn_replicas){false, 2}, mfd_prog_put(&d, "svc", prog_of(n));
    n = needs("wasm", NULL, NULL), n.replicas = (msn_replicas){false, 1}, n.cpu = 1.0f, mfd_prog_put(&d, "cpu1", prog_of(n));
    n = needs("wasm", NULL, NULL), n.replicas = (msn_replicas){false, 1}, n.cpu = 1.0f, mfd_prog_put(&d, "cpu2", prog_of(n));
    n = needs("metal", NULL, NULL), n.replicas = (msn_replicas){false, 1}, mfd_prog_put(&d, "mac", prog_of(n));
    n = needs("cuda", "metal", NULL), n.replicas = (msn_replicas){false, 1}, mfd_prog_put(&d, "hash", prog_of(n));
    n = needs("wasm", NULL, NULL), n.colocate_with = mt_strdup("hash"), mt_push(&n.feeds, "hash"), mfd_prog_put(&d, "src", prog_of(n));
    n = needs("wasm", NULL, NULL), mfd_prog_put(&d, "every", prog_of(n));
    msm_perfs perf = {0};
    mst_tracker *t = mst_new();
    ms_map moves = {0}; /* program -> "t1,t2,..." */
    uint64_t down_at[4] = {0};
    bool down[4] = {0};
    uint64_t rng = 12345;
#define RAND(n) (rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17, rng % (n))
    bool inflight = false;
    mst_move im = {0};
    uint64_t it0 = 0;
    size_t total = 0;
    for (uint64_t step = 0; step < 600; step++) {
        uint64_t now = step * 1000;
        bool phase = (now / 20000) % 2 == 1;
        ms[1].cpu_util = (phase ? 0.95f : 0.1f) + (float)RAND(10) / 200.0f;
        ms[1].psi_cpu = mof32_some(phase ? 0.4f : 0.01f);
        for (int k = 0; k < 4; k++)
            if (k != 1) ms[k].cpu_util = 0.1f + (float)RAND(20) / 200.0f;
        if (step == 120) ms[2].up = false, down[2] = true, down_at[2] = now;
        if (step == 200) ms[2].up = true, down[2] = false;
        for (int k = 0; k < 4; k++) ms[k].down_ms = down[k] ? now - down_at[k] : 0;
        mst_owner(t, ms, 4, now, &w);
        msp_input in = input(&d, ms, &perf, &w);
        mt_strs ch;
        msp_explains ex;
        mfd_doc nd = msp_place(&in, &ch, &ex);
        mt_strs_free(&ch), msp_explains_free(&ex), mfd_doc_free(&d);
        d = nd;
        if (inflight && now - it0 >= 2000) {
            mfd_doc e = mst_end_move(&d, &im, true, "done");
            mfd_doc_free(&d);
            d = e;
            inflight = false;
            mst_move_free(&im);
        }
        in = input(&d, ms, &perf, &w);
        mst_move m;
        if (mst_step(t, &in, now, inflight, &m)) {
            mst_moved(t, &m, now);
            for (size_t i = 0; i < m.programs.n; i++) {
                const char *prev = ms_get(&moves, m.programs.v[i]);
                char *v = prev ? mt_fmt("%s,%llu", prev, (unsigned long long)now) : mt_fmt("%llu", (unsigned long long)now);
                ms_set(&moves, m.programs.v[i], v, true);
                free(v);
                total++;
            }
            mfd_doc s = mst_start_move(&d, &m, d.version + 1);
            mfd_doc_free(&d);
            d = s;
            im = m;
            it0 = now;
            inflight = true;
        }
        if (phase && now % 20000 == 19000)
            for (size_t i = 0; i < d.nprogs; i++)
                if (!d.prog[i].needs.replicas.all && d.prog[i].at.some && ml_has(&d.prog[i].at.l, 1)) {
                    fprintf(stderr, "%s still on the loaded member at %llu\n", d.pname[i], (unsigned long long)now);
                    CHECK(0);
                }
        for (size_t i = 0; i < d.nprogs; i++) {
            const mfd_prog *p = &d.prog[i];
            ml_list at = msp_members_of_at(&p->at, ms, 4, &p->needs);
            if (!p->needs.replicas.all && !p->needs.colocate_with) CHECK(at.n <= p->needs.replicas.n);
            for (size_t j = 0; j < at.n; j++) {
                const msm_member *mm = &ms[at.v[j]];
                CHECK(msm_exec_for(mm, p->needs.backends.v, p->needs.backends.n) != NULL);
                CHECK(mm->up || mm->down_ms < w.replace_ms || p->has_from);
            }
            if (!strcmp(d.pname[i], "mac")) CHECK(at.n == 1 && at.v[0] == 3);
            if (!strcmp(d.pname[i], "src") && !p->has_from) CHECK(ml_opt_eq(&p->at, &mfd_prog_get(&d, "hash")->at));
            ml_free(&at);
        }
    }
    CHECK(total <= 30);
    CHECK(total > 0);
    for (size_t i = 0; i < moves.n; i++) {
        mt_strs ts = mt_split(moves.v[i], ',');
        CHECK(ts.n <= 12);
        for (size_t j = 1; j < ts.n; j++) CHECK(strtoull(ts.v[j], NULL, 10) - strtoull(ts.v[j - 1], NULL, 10) >= w.dwell_ms);
        mt_strs_free(&ts);
    }
    fprintf(stderr, "10 simulated minutes: %zu moves\n", total);
    if (inflight) mst_move_free(&im);
    ms_free(&moves);
    mst_free(t);
    mfd_doc_free(&d);
    free_members(ms, 4);
}

int main(void) {
    colocation_and_pins();
    hysteresis_and_budget();
    owner_policy();
    groups_proposed();
    new_leader_takes_over();
    verdict_is_its_programs();
    pin_never_splits();
    simulation();
    return mesh_check_done();
}
