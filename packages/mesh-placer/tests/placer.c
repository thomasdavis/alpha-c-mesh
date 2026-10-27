/* place.rs Placer tests: change_protocol_moves_state_across_three, split_auto_uses_measured_times,
 * change_times_out_and_rolls_back, split_frames_assemble_on_every_machine,
 * follower_keeps_the_newest_and_rejects_strays, member_loss_replans_and_return_rejoins. */
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/place_ops.h"
#include "mesh/check.h"
#include "mesh/placer.h"

#include <stdlib.h>

static mps_spec spec(void) {
    mps_region st[] = {{0x0, 0x8}, {0x100, 0x4}}, out[] = {{0x1000, 0x40}, {0x1400, 0x40}};
    return mps_new(MESH_PLACE_CAP_SPLIT, st, 2, 0x4, out, 2, 0x2000, 4);
}

static void viewfn(void *ctx, uint32_t o, uint32_t n, uint8_t *out) {
    uint8_t salt = (uint8_t)(uintptr_t)ctx;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t)(o + i) ^ salt;
}

static mpw_bytes read_state(uint8_t salt) {
    mps_spec s = spec();
    mpw_bytes b = mpc_read_state(&s, viewfn, (void *)(uintptr_t)salt);
    mps_free(&s);
    return b;
}

static mpw_bytes gather(uint32_t mask, uint8_t salt) {
    mps_spec s = spec();
    mpw_bytes b = mpc_gather(&s, mask, viewfn, (void *)(uintptr_t)salt);
    mps_free(&s);
    return b;
}

typedef struct {
    uint8_t salt;
} statectx;

static mpw_bytes state_cb(void *ctx) { return read_state(((statectx *)ctx)->salt); }

static mpw_place place_msg(const mpw_bytes *b) {
    size_t n;
    char *err = NULL;
    CHECK(b->b[0] == MESH_F_PLACE && mpw_place_rest_len(b->b + 1, &n, &err));
    mpw_place m = {0};
    CHECK(mpw_place_decode(b->b + 1, b->b + 1 + MPW_PLACE_HEAD, n, &m, &err));
    return m;
}

static mpw_frame frame_msg(const mpw_bytes *b) {
    size_t n;
    char *err = NULL;
    CHECK(b->b[0] == MESH_F_FRAME && mpw_frame_rest_len(b->b + 1, &n, &err));
    mpw_frame m = {0};
    CHECK(mpw_frame_decode(b->b + 1, b->b + 1 + MPW_FRAME_HEAD, n, &m, &err));
    return m;
}

/* (from, act) */
typedef struct {
    uint32_t k;
    mpl_act a;
} logged;

typedef struct {
    logged *v;
    size_t n;
} logv;

static void log_push(logv *l, uint32_t k, mpl_act a) {
    l->v = realloc(l->v, (l->n + 1) * sizeof *l->v);
    l->v[l->n++] = (logged){k, a};
}

static void log_free(logv *l) {
    for (size_t i = 0; i < l->n; i++) {
        mpw_bytes_free(&l->v[i].a.bytes);
        free(l->v[i].a.text);
    }
    free(l->v);
    *l = (logv){0};
}

/* Takes the acts into the log (as from k). */
static void take(logv *l, uint32_t k, mpl_acts *a) {
    for (size_t i = 0; i < a->n; i++) log_push(l, k, a->v[i]);
    free(a->v);
    *a = (mpl_acts){0};
}

/* A cluster of placers: routes their sends (by dest) until quiet, ticking each (drained) after every
 * pass; logs (from, act). */
static void pump(mpl_placer **ps, size_t np, logv *out, uint64_t t, logv *log) {
    for (int passes = 1;; passes++) {
        CHECK(passes < 50);
        logv next = {0};
        for (size_t i = 0; i < out->n; i++) {
            uint32_t from = out->v[i].k;
            mpl_act *act = &out->v[i].a;
            if (act->kind == MPL_SEND)
                for (uint32_t k = 0; k < np; k++) {
                    bool to = act->dest.to_mask ? (k != from && (act->dest.k >> k & 1)) : k == act->dest.k;
                    if (!to) continue;
                    mpl_acts r = act->bytes.b[0] == MESH_F_PLACE ? mpl_peer_place(ps[k], place_msg(&act->bytes), from, t) : mpl_peer_frame(ps[k], frame_msg(&act->bytes), t);
                    take(&next, k, &r);
                }
            log_push(log, from, *act);
        }
        free(out->v);
        *out = (logv){0};
        for (uint32_t k = 0; k < np; k++) {
            statectx c = {(uint8_t)(k + 1)};
            mpl_acts r = mpl_tick(ps[k], t, true, state_cb, &c);
            take(&next, k, &r);
        }
        if (!next.n) {
            free(next.v);
            break;
        }
        *out = next;
    }
}

static mt_strs w(const char *s) { return mt_words(s); }

static mpl_placer **cluster(uint32_t n, uint64_t t) {
    mpl_placer **ps = calloc(n, sizeof *ps);
    for (uint32_t k = 0; k < n; k++) {
        ps[k] = mpl_new("doom", spec(), k, n);
        mpl_acts a = mpl_view(ps[k], mpm_bits(n), true, 0, NULL, t);
        mpl_acts_free(&a);
    }
    return ps;
}

static void free_cluster(mpl_placer **ps, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) mpl_free(ps[k]);
    free(ps);
}

static void run(mpl_placer **ps, size_t np, uint32_t from, mpl_acts *acts, uint64_t t, logv *log) {
    logv out = {0};
    take(&out, from, acts);
    pump(ps, np, &out, t, log);
}

/* The answers in a log (handle, ok, text). */
static const mpl_act *answer_in(const logv *l, size_t i) {
    size_t c = 0;
    for (size_t j = 0; j < l->n; j++)
        if (l->v[j].a.kind == MPL_ANSWER && c++ == i) return &l->v[j].a;
    return NULL;
}

static size_t answers(const logv *l) {
    size_t c = 0;
    for (size_t j = 0; j < l->n; j++) c += l->v[j].a.kind == MPL_ANSWER;
    return c;
}

static const mpl_act *answer_of(const mpl_acts *a) {
    for (size_t i = 0; i < a->n; i++)
        if (a->v[i].kind == MPL_ANSWER) return &a->v[i];
    return NULL;
}

static size_t states_written(const logv *l, uint32_t k, mpw_bytes *first) {
    size_t c = 0;
    for (size_t j = 0; j < l->n; j++)
        if (l->v[j].k == k && l->v[j].a.kind == MPL_WRITE_STATE) {
            if (!c && first) *first = l->v[j].a.bytes;
            c++;
        }
    return c;
}

static bool bytes_eq(mpw_bytes a, mpw_bytes b) { return a.n == b.n && (!a.n || !memcmp(a.b, b.b, a.n)); }

static mpm_mode split4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return mpm_split(mpm_assign_new((uint32_t[]){a, b, c, d}, 4)); }

static bool all_mode(mpl_placer **ps, size_t n, mpm_mode m) {
    for (size_t k = 0; k < n; k++)
        if (!mpm_mode_eq(mpl_mode(ps[k]), &m)) return false;
    return true;
}

static mpl_acts request(mpl_placer *p, const char *words, uint64_t handle, uint64_t t) {
    mt_strs x = w(words);
    mpl_acts a = mpl_request(p, &x, handle, t);
    mt_strs_free(&x);
    return a;
}

static void change_protocol(void) {
    uint64_t t = 1000;
    mpl_placer **ps = cluster(3, t);
    logv log = {0};
    mpl_acts a = request(ps[0], "pin 2", 41, t);
    run(ps, 3, 0, &a, t, &log);
    for (int k = 0; k < 3; k++) CHECK(mpm_mode_eq(mpl_mode(ps[k]), &(mpm_mode){.kind = MPM_PIN, .pin = 2}) && !mpl_paused(ps[k]) && mpl_members(ps[k]) == 7);
    mpw_bytes st2 = read_state(3), f0 = {0}, f1 = {0};
    CHECK(states_written(&log, 0, &f0) == 1 && bytes_eq(f0, st2) && states_written(&log, 1, &f1) == 1 && bytes_eq(f1, st2) && states_written(&log, 2, NULL) == 0);
    CHECK(answers(&log) == 1 && answer_in(&log, 0)->handle == 41 && answer_in(&log, 0)->ok && mt_starts(answer_in(&log, 0)->text, "pin 2"));
    CHECK(mpl_role(ps[0]) == MPM_ROLE_MIRROR && mpl_role(ps[1]) == MPM_ROLE_MIRROR && mpl_role(ps[2]) == MPM_ROLE_RUNNER);
    log_free(&log);
    /* pin 2 -> split 1,0 (system 1 owns, 2 mirrors): the runner's state goes to everyone else. */
    a = request(ps[0], "split 1,0", 42, t);
    run(ps, 3, 0, &a, t, &log);
    CHECK(all_mode(ps, 3, split4(1, 1, 0, 0)));
    CHECK(states_written(&log, 0, &f0) == 1 && bytes_eq(f0, st2) && states_written(&log, 1, &f1) == 1 && bytes_eq(f1, st2));
    CHECK(mt_contains(answer_in(&log, 0)->text, "state from system 2"));
    CHECK(mpl_role(ps[0]) == MPM_ROLE_FOLLOWER && mpl_role(ps[1]) == MPM_ROLE_OWNER && mpl_role(ps[2]) == MPM_ROLE_MIRROR);
    log_free(&log);
    mpw_bytes_free(&st2);
    /* Already there; refused capability; a split that does not fit; not the leader; a second change while one runs. */
    a = request(ps[0], "split 1,0", 1, t);
    CHECK(answer_of(&a)->ok && !strcmp(answer_of(&a)->text, "split 1,1,0,0 already"));
    mpl_acts_free(&a);
    mps_spec pin_spec = spec();
    pin_spec.cap = MESH_PLACE_CAP_PIN;
    mpl_placer *pin_only = mpl_new("p", pin_spec, 0, 3);
    a = mpl_view(pin_only, 7, true, 0, NULL, t);
    mpl_acts_free(&a);
    a = request(pin_only, "split 0,1,1,1", 2, t);
    CHECK(!answer_of(&a)->ok);
    mpl_acts_free(&a);
    mpl_free(pin_only);
    a = request(ps[0], "pin 3", 3, t);
    CHECK(!answer_of(&a)->ok);
    mpl_acts_free(&a);
    a = request(ps[2], "local", 4, t);
    CHECK(!answer_of(&a)->ok && mt_contains(answer_of(&a)->text, "leader, system 0"));
    mpl_acts_free(&a);
    mpl_acts first = request(ps[0], "local", 5, t);
    size_t sends = 0;
    for (size_t i = 0; i < first.n; i++)
        if (first.v[i].kind == MPL_SEND) {
            sends++;
            CHECK(first.v[i].dest.to_mask && first.v[i].dest.k == 6);
        }
    CHECK(mpl_paused(ps[0]) && sends == 1);
    mpl_acts_free(&first);
    a = request(ps[0], "pin 1", 6, t);
    CHECK(!answer_of(&a)->ok && mt_contains(answer_of(&a)->text, "in progress"));
    mpl_acts_free(&a);
    free_cluster(ps, 3);
}

static void split_auto(void) {
    uint64_t t = 1000;
    mpl_placer **ps = cluster(3, t);
    uint32_t ns[] = {100, 100, 150};
    for (int k = 0; k < 3; k++) mpl_set_part_ns(ps[k], ns[k]);
    logv log = {0};
    mpl_acts a = request(ps[0], "split auto", 9, t);
    run(ps, 3, 0, &a, t, &log);
    CHECK(all_mode(ps, 3, split4(0, 0, 1, 2)));
    CHECK(mt_starts(answer_in(&log, 0)->text, "split 0,0,1,2"));
    log_free(&log);
    free_cluster(ps, 3);
}

static const mpl_act *first_send(const mpl_acts *a) {
    for (size_t i = 0; i < a->n; i++)
        if (a->v[i].kind == MPL_SEND) return &a->v[i];
    return NULL;
}

static void times_out(void) {
    uint64_t t = 1000;
    mpl_placer **ps = cluster(3, t);
    mpl_acts a = request(ps[0], "pin 1", 7, t);
    mpw_place pause = place_msg(&first_send(&a)->bytes);
    CHECK(pause.op == MESH_PL_PAUSE);
    mpw_place pause2 = place_msg(&first_send(&a)->bytes);
    mpl_acts x = mpl_peer_place(ps[1], pause, 0, t);
    mpl_acts_free(&x);
    x = mpl_peer_place(ps[2], pause2, 0, t);
    mpl_acts_free(&x);
    statectx s2 = {2}, s1 = {1};
    mpl_acts paused = mpl_tick(ps[1], t, true, state_cb, &s2);
    x = mpl_peer_place(ps[0], place_msg(&first_send(&paused)->bytes), 1, t);
    mpl_acts_free(&x);
    mpl_acts_free(&paused);
    x = mpl_tick(ps[2], t, false, state_cb, &s2);
    CHECK(x.n == 0 && mpl_paused(ps[2]));
    mpl_acts_free(&x);
    x = mpl_tick(ps[0], t, true, state_cb, &s1);
    CHECK(x.n == 0);
    mpl_acts_free(&x);
    mpl_acts acts = mpl_tick(ps[0], t + MPL_CHANGE_TIMEOUT_US + 1000, true, state_cb, &s1);
    CHECK(!answer_of(&acts)->ok && mt_contains(answer_of(&acts)->text, "system(s) 2 did not drain"));
    CHECK(!mpl_paused(ps[0]) && mpl_mode(ps[0])->kind == MPM_LOCAL);
    const mpl_act *fail = first_send(&acts);
    mpw_place fm = place_msg(&fail->bytes);
    CHECK(fail->dest.to_mask && fail->dest.k == 6 && fm.op == MESH_PL_FAIL && fm.mode.kind == MPM_LOCAL);
    mpw_place_free(&fm);
    for (int k = 1; k < 3; k++) {
        x = mpl_peer_place(ps[k], place_msg(&fail->bytes), 0, t);
        mpl_acts_free(&x);
        CHECK(!mpl_paused(ps[k]) && mpl_mode(ps[k])->kind == MPM_LOCAL);
    }
    mpl_acts_free(&acts);
    mpl_acts_free(&a);
    free_cluster(ps, 3);
}

/* One frame of a split over n machines (fanout 4), driven by hand: every machine must show the same
 * whole frame. */
static void split_frame(uint32_t n, mpm_mode mode) {
    uint64_t t = 1000;
    mps_spec s = spec();
    mpl_placer **ps = cluster(n, t);
    char *label = mpm_label(&mode);
    logv log = {0};
    mpl_acts a = request(ps[0], label, 1, t);
    run(ps, n, 0, &a, t, &log);
    log_free(&log);
    free(label);
    CHECK(all_mode(ps, n, mode));
    uint32_t owner = mpm_owner(&mode.a);
    mpw_bytes st = read_state((uint8_t)(owner + 1));
    bool shown[8] = {0};
    uint32_t shown_sys[8];
    uint8_t *shown_buf[8] = {0};
    /* wire: (from, act) */
    logv wire = {0};
    mpw_bytes stc = {0};
    mpw_put(&stc, st.b, st.n);
    mpl_acts own = mpl_own_frame(ps[owner], 10, stc, gather(mpm_mask_of(&mode.a, owner), (uint8_t)(owner + 1)), t);
    for (size_t i = 0; i < own.n; i++) {
        if (own.v[i].kind == MPL_SHOW) {
            CHECK(!shown[owner]);
            shown[owner] = true, shown_sys[owner] = own.v[i].sys, shown_buf[owner] = own.v[i].bytes.b, own.v[i].bytes.b = NULL;
        }
    }
    take(&wire, owner, &own);
    while (wire.n) {
        logged item = wire.v[--wire.n];
        if (item.a.kind != MPL_SEND) {
            mpw_bytes_free(&item.a.bytes);
            free(item.a.text);
            continue;
        }
        for (uint32_t k = 0; k < n; k++) {
            bool to = item.a.dest.to_mask ? (k != item.k && (item.a.dest.k >> k & 1)) : k == item.a.dest.k;
            if (!to) continue;
            mpl_acts r = mpl_peer_frame(ps[k], frame_msg(&item.a.bytes), t);
            bool follow = false;
            for (size_t i = 0; i < r.n; i++) follow |= r.v[i].kind == MPL_FOLLOW && r.v[i].frame == 10;
            if (follow) {
                bool wrote = false;
                for (size_t i = 0; i < r.n; i++) wrote |= r.v[i].kind == MPL_WRITE_STATE && bytes_eq(r.v[i].bytes, st);
                CHECK(wrote);
            }
            for (size_t i = 0; i < r.n; i++)
                if (r.v[i].kind == MPL_SHOW) {
                    CHECK(!shown[k] && r.v[i].frame == 10);
                    shown[k] = true, shown_sys[k] = r.v[i].sys, shown_buf[k] = r.v[i].bytes.b, r.v[i].bytes.b = NULL;
                }
            take(&wire, k, &r);
            if (follow) {
                mpl_acts done = mpl_follow_done(ps[k], gather(mpm_mask_of(&mode.a, k), (uint8_t)(k + 1)), t);
                for (size_t i = 0; i < done.n; i++)
                    if (done.v[i].kind == MPL_SHOW) {
                        CHECK(!shown[k]);
                        shown[k] = true, shown_sys[k] = done.v[i].sys, shown_buf[k] = done.v[i].bytes.b, done.v[i].bytes.b = NULL;
                    }
                take(&wire, k, &done);
            }
        }
        mpw_bytes_free(&item.a.bytes);
    }
    free(wire.v);
    /* The whole frame: part b from system of[b]'s render, the same on every machine. */
    uint8_t want[0x80] = {0};
    char *err = NULL;
    uint32_t nib = 0;
    for (uint32_t b = 0; b < 4; b++) {
        uint32_t k = mode.a.of[b];
        mpw_bytes g = gather(1u << b, (uint8_t)(k + 1));
        CHECK(mpc_scatter(&s, want, sizeof want, 1u << b, g.b, g.n, &err));
        mpw_bytes_free(&g);
        nib |= k << (4 * b);
    }
    for (uint32_t k = 0; k < n; k++) {
        if (!CHECK(shown[k])) {
            fprintf(stderr, "system %u showed nothing\n", k);
            continue;
        }
        CHECK(shown_sys[k] == nib && !memcmp(shown_buf[k], want, sizeof want));
        free(shown_buf[k]);
    }
    for (uint32_t k = 0; k < n; k++) CHECK_U(mpl_stats_of(ps[k])->ignored, 0);
    mpw_bytes_free(&st);
    mps_free(&s);
    free_cluster(ps, n);
}

static void follower_newest(void) {
    uint64_t t = 1000;
    mpl_placer **ps = cluster(3, t);
    logv log = {0};
    mpl_acts a = request(ps[0], "split 0,0,1,2", 1, t);
    run(ps, 3, 0, &a, t, &log);
    log_free(&log);
    mpw_bytes st = read_state(1);
    mpw_frame frames[3];
    for (uint32_t f = 10; f < 13; f++) {
        mpw_bytes stc = {0};
        mpw_put(&stc, st.b, st.n);
        mpl_acts own = mpl_own_frame(ps[0], f, stc, gather(3, 1), t);
        frames[f - 10] = frame_msg(&first_send(&own)->bytes);
        mpl_acts_free(&own);
    }
    mpl_acts r = mpl_peer_frame(ps[1], frames[0], t);
    CHECK(r.n == 2 && r.v[0].kind == MPL_WRITE_STATE && r.v[1].kind == MPL_FOLLOW && r.v[1].frame == 10);
    mpl_acts_free(&r);
    for (int i = 1; i < 3; i++) {
        r = mpl_peer_frame(ps[1], frames[i], t);
        CHECK(r.n == 0);
        mpl_acts_free(&r);
    }
    CHECK_U(mpl_stats_of(ps[1])->ignored, 1);
    mpl_acts done = mpl_follow_done(ps[1], gather(4, 2), t);
    CHECK(done.n && done.v[done.n - 1].kind == MPL_FOLLOW && done.v[done.n - 1].frame == 12);
    mpl_acts_free(&done);
    /* A frame from a system with other parts, or state from a non-owner, is ignored. */
    mpw_bytes g4 = gather(4, 3), g8 = gather(8, 3);
    mpw_frame stray = {.name = "doom", .frame = 3, .parts = 4, .sys = 2, .data = g4.b, .ndata = g4.n};
    r = mpl_peer_frame(ps[0], mpw_frame_clone(&stray), t);
    CHECK(r.n == 0);
    mpl_acts_free(&r);
    mpw_frame forged = {.name = "doom", .frame = 3, .parts = 8, .sys = 2, .state = st.b, .nstate = st.n, .data = g8.b, .ndata = g8.n};
    r = mpl_peer_frame(ps[1], mpw_frame_clone(&forged), t);
    CHECK(r.n == 0);
    mpl_acts_free(&r);
    CHECK(mpl_stats_of(ps[0])->ignored == 1 && mpl_stats_of(ps[1])->ignored == 2);
    mpw_bytes_free(&g4), mpw_bytes_free(&g8), mpw_bytes_free(&st);
    free_cluster(ps, 3);
}

static bool logged_line(const logv *l, const char *needle) {
    for (size_t i = 0; i < l->n; i++)
        if (l->v[i].a.kind == MPL_LOG && mt_contains(l->v[i].a.text, needle)) return true;
    return false;
}

static void member_loss(void) {
    uint64_t t = 1000;
    mpl_placer **ps = cluster(3, t);
    logv log = {0};
    mpl_acts a = request(ps[0], "split", 1, t);
    run(ps, 3, 0, &a, t, &log);
    log_free(&log);
    CHECK(all_mode(ps, 3, split4(0, 0, 1, 2)));
    /* System 2 dies: the leader re-plans `split` over 0 and 1 at once. */
    mt_strs sw = w("split");
    logv out = {0};
    for (uint32_t k = 0; k < 2; k++) {
        mpl_acts v = mpl_view(ps[k], 3, true, 0, &sw, t);
        take(&out, k, &v);
    }
    mpl_placer *live[3] = {ps[0], ps[1], NULL};
    pump(live, 2, &out, t, &log);
    CHECK(all_mode(live, 2, split4(0, 0, 1, 1)) && mpl_members(live[0]) == 3 && mpl_members(live[1]) == 3);
    CHECK(logged_line(&log, "re-planning"));
    log_free(&log);
    mt_strs_free(&sw);
    free_cluster(ps, 3);
    /* Pinned to the member that dies: the leader runs it, from its own (mirror) copy of the state. */
    ps = cluster(3, t);
    a = request(ps[0], "pin 2", 2, t);
    run(ps, 3, 0, &a, t, &log);
    log_free(&log);
    mt_strs pw = w("pin 2");
    for (uint32_t k = 0; k < 2; k++) {
        mpl_acts v = mpl_view(ps[k], 3, true, 0, &pw, t);
        take(&out, k, &v);
    }
    live[0] = ps[0], live[1] = ps[1];
    pump(live, 2, &out, t, &log);
    CHECK(all_mode(live, 2, mpm_pin(0)));
    CHECK(logged_line(&log, "system 2's state is gone"));
    mpw_bytes s1 = read_state(1), got = {0};
    CHECK(states_written(&log, 1, &got) == 1 && bytes_eq(got, s1));
    log_free(&log);
    mpw_bytes_free(&s1);
    /* System 2 comes back: after the dwell, the leader moves it back to its pin (the request). */
    mpl_free(ps[2]);
    ps[2] = mpl_new("doom", spec(), 2, 3);
    a = mpl_view(ps[2], 7, true, 0, &pw, t);
    mpl_acts_free(&a);
    for (int k = 0; k < 2; k++) {
        a = mpl_view(ps[k], 7, true, 0, &pw, t);
        CHECK(a.n == 0);
        mpl_acts_free(&a);
    }
    statectx c1 = {1};
    a = mpl_tick(ps[0], t, true, state_cb, &c1);
    CHECK(a.n == 0);
    mpl_acts_free(&a);
    a = mpl_tick(ps[0], t + MPL_JOIN_DWELL_US, true, state_cb, &c1);
    run(ps, 3, 0, &a, t + MPL_JOIN_DWELL_US, &log);
    log_free(&log);
    CHECK(all_mode(ps, 3, mpm_pin(2)) && mpl_members(ps[0]) == 7 && mpl_members(ps[2]) == 7);
    /* No leader, and the runner is gone: local here. */
    a = mpl_view(ps[1], 3, false, 0, &pw, t);
    CHECK(a.n && a.v[a.n - 1].kind == MPL_MODE && a.v[a.n - 1].mode.kind == MPM_LOCAL && mpl_mode(ps[1])->kind == MPM_LOCAL);
    mpl_acts_free(&a);
    a = mpl_view(ps[1], 3, false, 0, &pw, t);
    CHECK(a.n == 0);
    mpl_acts_free(&a);
    mt_strs_free(&pw);
    free_cluster(ps, 3);
}

int main(void) {
    change_protocol();
    split_auto();
    times_out();
    split_frame(2, split4(0, 0, 1, 1));
    split_frame(3, split4(0, 0, 1, 2));
    split_frame(3, split4(2, 1, 2, 1));
    split_frame(4, split4(0, 1, 2, 3));
    split_frame(4, split4(1, 1, 2, 0));
    split_frame(3, split4(2, 2, 2, 2));
    follower_newest();
    member_loss();
    return mesh_check_done();
}
