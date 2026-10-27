/* place.rs Placer: one slot's placement state machine (see the header). */
#include "mesh/placer.h"
#include "mesh/abi/place_ops.h"
#include "mesh/abi/placement.h"
#include "mesh/rust_fmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { PAUSING, SETTING } phase_t;

/* Who asked for a change: a local request, or nobody (a re-plan after the up members changed). */
typedef struct {
    bool local;
    uint64_t handle;
} asker_t;

typedef struct {
    uint32_t k;
    mpw_bytes state;
    uint32_t part_ns;
} member_state;

/* A change the leader runs among the up members. */
typedef struct {
    uint32_t id;
    mpm_mode from;
    mpm_target to;
    mpm_mode mode; /* resolved at Setting */
    phase_t phase;
    member_state states[MESH_PLACE_PARTS_MAX];
    size_t nstates;
    uint32_t waiting, members;
    uint64_t since;
    asker_t asker;
} change_t;

struct mpl_placer {
    char name[MESH_NAME_BYTES + 1];
    mps_spec spec;
    uint32_t me, n;
    mpm_mode mode;
    bool paused;
    uint32_t part_ns;
    bool has_coord;
    uint32_t coord;
    uint32_t up, members;
    bool has_request;
    mt_strs request;
    mpc_composer composer;
    bool has_follow_inflight;
    uint32_t follow_inflight;
    bool has_follow_next;
    mpw_frame follow_next;
    bool has_change;
    change_t change;
    bool has_pause_for;
    uint32_t pause_id, pause_leader;
    bool has_join_at;
    uint64_t join_at;
    uint32_t next_id;
    mpl_stats stats;
    void (*log)(void *ctx, const char *line);
    void *log_ctx;
};

static uint64_t since_us(uint64_t now, uint64_t t) { return now > t ? now - t : 0; }

static void push(mpl_acts *a, mpl_act x) {
    if (a->n == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 8;
        a->v = mt_realloc(a->v, a->cap * sizeof *a->v);
    }
    a->v[a->n++] = x;
}

void mpl_acts_free(mpl_acts *a) {
    for (size_t i = 0; i < a->n; i++) {
        mpw_bytes_free(&a->v[i].bytes);
        free(a->v[i].text);
    }
    free(a->v);
    *a = (mpl_acts){0};
}

static void act_log(mpl_acts *a, char *text) { push(a, (mpl_act){.kind = MPL_LOG, .text = text}); }

static void change_free(change_t *c) {
    for (size_t i = 0; i < c->nstates; i++) mpw_bytes_free(&c->states[i].state);
    c->nstates = 0;
}

mpl_placer *mpl_new(const char *name, mps_spec spec, uint32_t me, uint32_t n) {
    mpl_placer *p = mt_alloc(sizeof *p);
    size_t k = strlen(name);
    if (k > MESH_NAME_BYTES) k = MESH_NAME_BYTES;
    memcpy(p->name, name, k);
    p->spec = spec;
    p->me = me;
    p->n = n;
    p->mode = mpm_local();
    p->up = p->members = 1u << me;
    p->next_id = 1;
    return p;
}

void mpl_free(mpl_placer *p) {
    if (!p) return;
    mps_free(&p->spec);
    mt_strs_free(&p->request);
    mpc_clear(&p->composer);
    if (p->has_follow_next) mpw_frame_free(&p->follow_next);
    if (p->has_change) change_free(&p->change);
    free(p);
}

void mpl_set_log(mpl_placer *p, void (*log)(void *ctx, const char *line), void *ctx) {
    p->log = log;
    p->log_ctx = ctx;
}

const mpm_mode *mpl_mode(const mpl_placer *p) { return &p->mode; }
mpm_role mpl_role(const mpl_placer *p) { return mpm_role_of(&p->mode, p->me); }
bool mpl_paused(const mpl_placer *p) { return p->paused; }
bool mpl_busy(const mpl_placer *p) { return p->has_change || p->has_pause_for; }
uint32_t mpl_members(const mpl_placer *p) { return p->members; }
uint32_t mpl_my_parts(const mpl_placer *p) { return mpm_parts_of(&p->mode, p->me, p->spec.fanout); }
const mpl_stats *mpl_stats_of(const mpl_placer *p) { return &p->stats; }
void mpl_set_part_ns(mpl_placer *p, uint32_t ns) { p->part_ns = ns; }
const mps_spec *mpl_spec(const mpl_placer *p) { return &p->spec; }

/* The other up members. */
static uint32_t others(const mpl_placer *p) { return p->up & mpm_bits(p->n) & ~(1u << p->me); }
static bool leading(const mpl_placer *p) { return p->has_coord && p->coord == p->me; }
/* The members a mode needs (pin k: k; split: those with parts). */
static uint32_t involved(const mpm_mode *m) { return m->kind == MPM_LOCAL ? 0 : m->kind == MPM_PIN ? 1u << m->pin : mpm_members(&m->a); }

bool mpl_parse(const mpl_placer *p, char *const *words, size_t n, mpm_target *out, char **err) { return mpm_parse(words, n, p->n, p->spec.fanout, out, err); }

bool mpl_plan(const mpl_placer *p, const mt_strs *words, mpm_target *out, char **err) {
    uint32_t up[MESH_PLACE_PARTS_MAX];
    size_t nup = 0;
    for (uint32_t k = 0; k < p->n; k++)
        if (p->up >> k & 1) up[nup++] = k;
    mpm_target t;
    if (!mpl_parse(p, words->v, words->n, &t, err)) return false;
    if (t.is_auto) t.auto_mask &= p->up;
    else if (t.mode.kind == MPM_PIN && !(p->up >> t.mode.pin & 1)) t.mode = mpm_pin(p->me);
    else if (t.mode.kind == MPM_SPLIT && !((mpm_members(&t.mode.a) & ~p->up) == 0 && words->n != 1)) {
        /* `split` alone: every up member; a spec naming a member that is down: its up members. */
        uint32_t named[MESH_PLACE_PARTS_MAX];
        size_t nn = 0;
        if (words->n == 1) memcpy(named, up, nup * sizeof *up), nn = nup;
        else {
            for (uint32_t b = 0; b < t.mode.a.fanout; b++) {
                uint32_t k = t.mode.a.of[b];
                bool have = false;
                for (size_t i = 0; i < nn; i++) have |= named[i] == k;
                if ((p->up >> k & 1) && !have) named[nn++] = k;
            }
            if (!nn) memcpy(named, up, nup * sizeof *up), nn = nup;
        }
        t.mode = mpm_split(mpm_even(named, nn, p->spec.fanout));
    }
    *out = t;
    return true;
}

static void msg(const mpl_placer *p, mpl_acts *a, mpl_dest to, uint32_t op, uint32_t id, mpm_mode mode, uint32_t aux, const uint8_t *bytes, size_t n) {
    mpw_place m = {.op = op, .id = id, .mode = mode, .aux = aux, .bytes = (uint8_t *)bytes, .nbytes = n};
    memcpy(m.name, p->name, sizeof m.name);
    push(a, (mpl_act){.kind = MPL_SEND, .dest = to, .bytes = mpw_place_encode(&m)});
}

static mpl_dest to_k(uint32_t k) { return (mpl_dest){false, k}; }
static mpl_dest to_mask(uint32_t m) { return (mpl_dest){true, m}; }

/* The mode changes here (resets the per-mode frame state). */
static void set_mode(mpl_placer *p, mpm_mode m, mpl_acts *a) {
    p->mode = m;
    mpc_clear(&p->composer);
    p->has_follow_inflight = false;
    if (p->has_follow_next) mpw_frame_free(&p->follow_next);
    p->has_follow_next = false;
    push(a, (mpl_act){.kind = MPL_MODE, .mode = m});
}

/* Answers a request: its asker, or a log line for a re-plan. Takes text. */
static void answer(const mpl_placer *p, asker_t who, bool ok, char *text, mpl_acts *a) {
    if (who.local) push(a, (mpl_act){.kind = MPL_ANSWER, .handle = who.handle, .ok = ok, .text = text});
    else {
        act_log(a, mt_fmt("place %s: %s", p->name, text));
        free(text);
    }
}

/* The leader starts a change: pause every other up member, collect their state. */
static void begin(mpl_placer *p, mpm_target to, asker_t asker, uint64_t now, bool force, mpl_acts *a) {
    if (p->has_change) {
        answer(p, asker, false, mt_fmt("%s: a placement change is in progress", p->name), a);
        return;
    }
    mpm_target cur = {.mode = p->mode};
    if (mpm_target_eq(&to, &cur) && p->members == p->up && !force) {
        char *l = mpm_label(&p->mode);
        answer(p, asker, true, mt_fmt("%s already", l), a);
        free(l);
        return;
    }
    uint32_t id = p->next_id++;
    p->paused = true;
    char *label = to.is_auto ? mt_strdup("split auto") : mpm_label(&to.mode);
    uint32_t o = others(p);
    p->has_change = true;
    p->change = (change_t){.id = id, .from = p->mode, .to = to, .mode = p->mode, .phase = PAUSING, .waiting = o, .members = p->up, .since = now, .asker = asker};
    char *from = mpm_label(&p->mode), hx[24];
    act_log(a, mt_fmt("place %s: %s -> %s (change %u): pausing the up members %s", p->name, from, label, id, rf_hex_alt(hx, p->up)));
    free(from), free(label);
    if (o) msg(p, a, to_mask(o), MESH_PL_PAUSE, id, p->mode, 0, NULL, 0);
}

static member_state *state_of(change_t *c, uint32_t k) {
    for (size_t i = 0; i < c->nstates; i++)
        if (c->states[i].k == k) return &c->states[i];
    return NULL;
}

/* Removes k's state from the change into *out (true), or false. */
static bool take_state(change_t *c, uint32_t k, mpw_bytes *out) {
    for (size_t i = 0; i < c->nstates; i++)
        if (c->states[i].k == k) {
            *out = c->states[i].state;
            c->states[i] = c->states[--c->nstates];
            return true;
        }
    return false;
}

/* The mode a change lands on (split auto: every member's time per part, ours included). */
static mpm_mode resolve(const mpl_placer *p, change_t *c) {
    if (!c->to.is_auto) return c->to.mode;
    uint32_t ks[MESH_PLACE_PARTS_MAX], ns[MESH_PLACE_PARTS_MAX];
    size_t k = 0;
    for (uint32_t s = 0; s < p->n; s++)
        if (c->to.auto_mask >> s & 1) {
            member_state *st = state_of(c, s);
            ks[k] = s;
            ns[k++] = s == p->me ? p->part_ns : st ? st->part_ns : 0;
        }
    mpm_mode m = mpm_split(mpm_auto(ks, ns, k, p->spec.fanout));
    if (p->log) {
        mt_buf b = {0};
        mt_printf(&b, "place %s: split auto: time per part ", p->name);
        for (size_t i = 0; i < k; i++) mt_printf(&b, i ? ", system %u %.1f us" : "system %u %.1f us", ks[i], (double)ns[i] / 1e3);
        char *l = mpm_label(&m);
        mt_printf(&b, " -> %s", l);
        free(l);
        p->log(p->log_ctx, mt_str(&b));
        mt_buf_free(&b);
    }
    return m;
}

/* The leader re-runs the committed request over the up members. */
static void replan(mpl_placer *p, uint64_t now, mpl_acts *a) {
    p->has_join_at = false;
    mt_strs words = p->has_request ? mt_strs_clone(&p->request) : (mt_strs){0};
    if (!p->has_request) mt_push(&words, "local");
    mpm_target t;
    char *err = NULL;
    if (mpl_plan(p, &words, &t, &err)) begin(p, t, (asker_t){false, 0}, now, true, a);
    else {
        char *j = mt_join(&words, " ");
        char *q = rf_debug_str(j);
        act_log(a, mt_fmt("place %s: re-plan of %s: %s", p->name, q, err));
        free(j), free(q), free(err);
    }
    mt_strs_free(&words);
}

mpl_acts mpl_request(mpl_placer *p, const mt_strs *words, uint64_t handle, uint64_t now) {
    mpl_acts a = {0};
    if (!leading(p)) {
        char *who = p->has_coord ? mt_fmt("the leader, system %u", p->coord) : mt_strdup("a leader (there is none: no majority)");
        push(&a, (mpl_act){.kind = MPL_ANSWER, .handle = handle, .text = mt_fmt("peer: placement changes are coordinated by %s", who)});
        free(who);
        return a;
    }
    mpm_target to;
    char *err = NULL;
    bool ok = mpl_plan(p, words, &to, &err);
    if (ok) {
        mpm_mode m = to.is_auto ? mpm_split(mpm_even((uint32_t[]){0}, 1, p->spec.fanout)) : to.mode;
        ok = mpm_allowed(&m, p->spec.cap, &err) && (to.is_auto || mpm_fits(&m, p->n, p->spec.fanout, &err));
    }
    if (!ok) {
        push(&a, (mpl_act){.kind = MPL_ANSWER, .handle = handle, .text = mt_fmt("%s: %s", p->name, err)});
        free(err);
        return a;
    }
    mt_strs_free(&p->request);
    p->has_request = !(words->n == 1 && strcmp(words->v[0], "local") == 0);
    if (p->has_request) p->request = mt_strs_clone(words);
    begin(p, to, (asker_t){true, handle}, now, false, &a);
    return a;
}

mpl_acts mpl_view(mpl_placer *p, uint32_t up, bool has_coord, uint32_t coord, const mt_strs *request, uint64_t now) {
    mpl_acts a = {0};
    p->up = (up | 1u << p->me) & mpm_bits(p->n);
    bool was_has = p->has_coord;
    uint32_t was = p->coord;
    p->has_coord = has_coord;
    p->coord = has_coord ? coord : 0;
    mt_strs_free(&p->request);
    p->has_request = request != NULL;
    if (request) p->request = mt_strs_clone(request);
    /* Members gone during a change: nothing more to wait for from them. */
    if (p->has_change) p->change.waiting &= p->up, p->change.members &= p->up;
    if (p->has_change && !leading(p)) {
        p->has_change = false;
        change_free(&p->change);
        p->paused = false;
        answer(p, p->change.asker, false, mt_strdup("peer: the leader changed during the change"), &a);
    }
    if (p->has_pause_for && (!(p->up >> p->pause_leader & 1) || !has_coord || coord != p->pause_leader)) p->has_pause_for = false, p->paused = false;
    uint32_t missing = involved(&p->mode) & ~p->up;
    bool coord_changed = was_has != has_coord || (has_coord && was != coord);
    char h1[24], h2[24];
    if (leading(p)) {
        if (missing && !p->has_change) {
            char *l = mpm_label(&p->mode);
            act_log(&a, mt_fmt("place %s: system(s) %s of %s are gone: re-planning over the up members %s", p->name, rf_hex_alt(h1, missing), l, rf_hex_alt(h2, p->up)));
            free(l);
            replan(p, now, &a);
        } else if ((p->up & ~p->members) || (coord_changed && p->members != p->up)) {
            if (!p->has_join_at) p->has_join_at = true, p->join_at = now + MPL_JOIN_DWELL_US;
        } else p->has_join_at = false;
    } else {
        p->has_join_at = false;
        if (!has_coord && missing && p->mode.kind != MPM_LOCAL) {
            char *l = mpm_label(&p->mode);
            act_log(&a, mt_fmt("place %s: system(s) %s are gone and there is no leader to re-plan: %s -> local on this machine", p->name, rf_hex_alt(h1, missing), l));
            free(l);
            p->paused = false;
            set_mode(p, mpm_local(), &a);
            p->members = 1u << p->me;
        }
    }
    return a;
}

/* Setting with nothing more to wait for: the change is done. */
static void finish(mpl_placer *p, uint64_t now, mpl_acts *a) {
    if (!p->has_change || p->change.phase != SETTING || p->change.waiting) return;
    change_t c = p->change;
    p->has_change = false;
    p->paused = false;
    double ms = (double)since_us(now, c.since) / 1e3;
    char *l = mpm_label(&c.mode), hx[24];
    act_log(a, mt_fmt("place %s: %s on the up members %s in %.1f ms", p->name, l, rf_hex_alt(hx, c.members), ms));
    answer(p, c.asker, true, mt_fmt("%s (state from system %u) in %.1f ms", l, mpm_authority(&c.from, &c.mode), ms), a);
    free(l);
    change_free(&c);
}

static void show(mpl_placer *p, uint32_t frame, uint32_t sys, uint8_t *buf, size_t n, uint32_t lat_us, mpl_acts *a) {
    p->stats.shown++;
    if (p->mode.kind == MPM_SPLIT) {
        p->stats.lat_n++;
        p->stats.lat_sum_us += lat_us;
        if (lat_us > p->stats.lat_max_us) p->stats.lat_max_us = lat_us;
    }
    push(a, (mpl_act){.kind = MPL_SHOW, .frame = frame, .sys = sys, .bytes = {buf, n, n}, .lat_us = lat_us});
}

/* Parts into the composer; a whole frame is shown. */
static bool compose(mpl_placer *p, uint32_t frame, uint32_t parts, uint32_t sys, const uint8_t *data, size_t n, uint64_t now, mpl_acts *a) {
    mpc_whole w;
    char *err = NULL;
    int r = mpc_add(&p->composer, &p->spec, frame, parts, sys, data, n, now, &w, &err);
    if (r == 1) {
        show(p, w.frame, w.sys, w.buf, w.nbuf, (uint32_t)since_us(now, w.t0), a);
        return true;
    }
    if (r < 0) {
        act_log(a, mt_fmt("place %s: %s", p->name, err));
        free(err);
    }
    return false;
}

static void try_follow(mpl_placer *p, uint64_t now, mpl_acts *a) {
    if (p->has_follow_inflight || !p->has_follow_next) return;
    mpw_frame m = p->follow_next;
    p->has_follow_next = false;
    mpw_bytes st = {0};
    mpw_put(&st, m.state, m.nstate);
    push(a, (mpl_act){.kind = MPL_WRITE_STATE, .bytes = st});
    mpc_whole w;
    char *err = NULL;
    int r = mpc_add(&p->composer, &p->spec, m.frame, m.parts, m.sys, m.data, m.ndata, now, &w, &err);
    if (r < 0) {
        act_log(a, mt_fmt("place %s: %s", p->name, err));
        free(err);
        mpw_frame_free(&m);
        return;
    }
    if (r == 1) free(w.buf); /* a follower's own parts complete it later */
    p->has_follow_inflight = true;
    p->follow_inflight = m.frame;
    push(a, (mpl_act){.kind = MPL_FOLLOW, .frame = m.frame});
    mpw_frame_free(&m);
}

mpl_acts mpl_tick(mpl_placer *p, uint64_t now, bool drained, mpl_state_fn state, void *ctx) {
    mpl_acts a = {0};
    if (p->has_pause_for && drained) {
        p->has_pause_for = false;
        mpw_bytes st = state(ctx);
        msg(p, &a, to_k(p->pause_leader), MESH_PL_PAUSED, p->pause_id, p->mode, p->part_ns, st.b, st.n);
        mpw_bytes_free(&st);
    }
    if (leading(p) && !p->has_change && p->has_join_at && now >= p->join_at) {
        char hx[24];
        act_log(&a, mt_fmt("place %s: members %s up again: re-planning", p->name, rf_hex_alt(hx, p->up & ~p->members)));
        replan(p, now, &a);
    }
    if (p->has_change) {
        change_t *c = &p->change;
        if (since_us(now, c->since) > MPL_CHANGE_TIMEOUT_US) {
            mt_buf miss = {0};
            for (uint32_t k = 0; k < p->n; k++)
                if (c->waiting >> k & 1) mt_printf(&miss, miss.len ? ",%u" : "%u", k);
            char *from = mpm_label(&c->from);
            char *why = mt_fmt("%s: the change did not finish in 5s (system(s) %s did not %s); back to %s", p->name, mt_str(&miss), c->phase == PAUSING ? "drain" : "apply it", from);
            free(from);
            mt_buf_free(&miss);
            change_t cc = *c;
            p->has_change = false;
            act_log(&a, mt_strdup(why));
            if (!mpm_mode_eq(&p->mode, &cc.from)) set_mode(p, cc.from, &a);
            uint32_t o = others(p);
            if (o) msg(p, &a, to_mask(o), MESH_PL_FAIL, cc.id, cc.from, 0, (const uint8_t *)why, strlen(why));
            p->paused = false;
            answer(p, cc.asker, false, why, &a);
            change_free(&cc);
        } else if (c->phase == PAUSING && drained && c->waiting == 0) {
            mpm_mode to = resolve(p, c);
            /* The state that continues: the old mode's authority's; if it is gone, the new authority's
             * own copy (a mirror or follower holds the last frame's state). */
            uint32_t auth = mpm_authority(&c->from, &to), src;
            mpw_bytes st;
            if (auth == p->me) src = p->me, st = state(ctx);
            else if (take_state(c, auth, &st)) src = auth;
            else {
                mpm_mode local = mpm_local();
                uint32_t na = mpm_authority(&local, &to);
                bool had = take_state(c, na, &st);
                if (had && na != p->me) src = na;
                else {
                    if (had) mpw_bytes_free(&st);
                    src = p->me, st = state(ctx);
                }
            }
            if (src != auth) act_log(&a, mt_fmt("place %s: system %u's state is gone: the game continues from system %u's copy", p->name, auth, src));
            c->phase = SETTING;
            c->mode = to;
            c->waiting = others(p) & c->members;
            if (src != p->me) {
                mpw_bytes copy = {0};
                mpw_put(&copy, st.b, st.n);
                push(&a, (mpl_act){.kind = MPL_WRITE_STATE, .bytes = copy});
            }
            set_mode(p, to, &a);
            p->members = c->members;
            /* PL_SET's aux: the members (bits 0..8) and the system whose state continues (bits 8..16). */
            if (c->waiting) msg(p, &a, to_mask(c->waiting), MESH_PL_SET, c->id, to, c->members | src << 8, st.b, st.n);
            mpw_bytes_free(&st);
            finish(p, now, &a);
        }
    }
    if (mpl_role(p) == MPM_ROLE_FOLLOWER && !p->paused) try_follow(p, now, &a);
    return a;
}

mpl_acts mpl_peer_place(mpl_placer *p, mpw_place m, uint32_t from, uint64_t now) {
    mpl_acts a = {0};
    if (m.op == MESH_PL_PAUSED && leading(p)) {
        if (p->has_change && p->change.id == m.id && p->change.phase == PAUSING) {
            change_t *c = &p->change;
            member_state *s = state_of(c, from);
            if (!s) s = &c->states[c->nstates++], s->k = from;
            else mpw_bytes_free(&s->state);
            s->state = (mpw_bytes){m.bytes, m.nbytes, m.nbytes};
            s->part_ns = m.aux;
            m.bytes = NULL;
            c->waiting &= ~(1u << from);
        } else act_log(&a, mt_fmt("place %s: stale PL_PAUSED %u from system %u", p->name, m.id, from));
    } else if (m.op == MESH_PL_DONE && leading(p)) {
        if (p->has_change && p->change.id == m.id && p->change.phase == SETTING) {
            p->change.waiting &= ~(1u << from);
            finish(p, now, &a);
        } else act_log(&a, mt_fmt("place %s: stale PL_DONE %u from system %u", p->name, m.id, from));
    } else if (m.op == MESH_PL_PAUSE) {
        p->paused = true;
        p->has_pause_for = true;
        p->pause_id = m.id;
        p->pause_leader = from;
    } else if (m.op == MESH_PL_SET) {
        char *err = NULL;
        if (!mpm_fits(&m.mode, p->n, p->spec.fanout, &err)) {
            char *l = mpm_label(&m.mode);
            act_log(&a, mt_fmt("place %s: system %u set %s which does not fit here (%s); local", p->name, from, l, err));
            free(l), free(err);
            set_mode(p, mpm_local(), &a);
        } else {
            /* The state continues from the system the leader took it from (aux bits 8..16). */
            if ((m.aux >> 8 & 0xff) != p->me) {
                push(&a, (mpl_act){.kind = MPL_WRITE_STATE, .bytes = {m.bytes, m.nbytes, m.nbytes}});
                m.bytes = NULL;
            }
            set_mode(p, m.mode, &a);
            p->members = m.aux & 0xff & mpm_bits(p->n);
        }
        p->paused = false;
        p->has_pause_for = false;
        msg(p, &a, to_k(from), MESH_PL_DONE, m.id, p->mode, 0, NULL, 0);
    } else if (m.op == MESH_PL_FAIL) {
        char *r = mt_strndup((const char *)m.bytes, m.nbytes);
        act_log(&a, mt_fmt("place %s: system %u rolled the change back: %s", p->name, from, r));
        free(r);
        if (!mpm_mode_eq(&p->mode, &m.mode)) set_mode(p, m.mode, &a);
        p->paused = false;
        p->has_pause_for = false;
    } else {
        char lead[24];
        if (p->has_coord) snprintf(lead, sizeof lead, "Some(%u)", p->coord);
        else strcpy(lead, "None");
        act_log(&a, mt_fmt("place %s: F_PLACE op %u from system %u is not for system %u (the leader is %s); ignored", p->name, m.op, from, p->me, lead));
    }
    mpw_place_free(&m);
    return a;
}

static mpw_bytes frame_bytes(const mpl_placer *p, uint32_t frame, uint32_t parts, const uint8_t *st, size_t nst, const uint8_t *data, size_t nd) {
    mpw_frame f = {.frame = frame, .parts = parts, .sys = p->me, .state = (uint8_t *)st, .nstate = nst, .data = (uint8_t *)data, .ndata = nd};
    memcpy(f.name, p->name, sizeof f.name);
    return mpw_frame_encode(&f);
}

mpl_acts mpl_own_frame(mpl_placer *p, uint32_t frame, mpw_bytes state, mpw_bytes data, uint64_t now) {
    mpl_acts a = {0};
    uint32_t parts = mpl_my_parts(p);
    mpl_dest to = to_mask(p->members & mpm_bits(p->n) & ~(1u << p->me));
    mpm_role r = mpl_role(p);
    if (r == MPM_ROLE_RUNNER) {
        size_t nb = mps_out_bytes(&p->spec);
        uint8_t *buf = mt_alloc(nb ? nb : 1);
        char *err = NULL;
        if (!mpc_scatter(&p->spec, buf, nb, parts, data.b, data.n, &err)) {
            act_log(&a, mt_fmt("place %s: %s", p->name, err));
            free(err), free(buf);
            goto done;
        }
        show(p, frame, mpc_sys_nibbles(parts, p->me), buf, nb, 0, &a);
        push(&a, (mpl_act){.kind = MPL_SEND, .dest = to, .bytes = frame_bytes(p, frame, parts, state.b, state.n, data.b, data.n)});
    } else if (r == MPM_ROLE_OWNER) {
        mpc_whole w;
        char *err = NULL;
        int x = mpc_add(&p->composer, &p->spec, frame, parts, p->me, data.b, data.n, now, &w, &err);
        if (x == 1) show(p, w.frame, w.sys, w.buf, w.nbuf, 0, &a);
        else if (x < 0) {
            act_log(&a, mt_fmt("place %s: %s", p->name, err));
            free(err);
        }
        push(&a, (mpl_act){.kind = MPL_SEND, .dest = to, .bytes = frame_bytes(p, frame, parts, state.b, state.n, data.b, data.n)});
    }
done:
    mpw_bytes_free(&state);
    mpw_bytes_free(&data);
    return a;
}

mpl_acts mpl_follow_done(mpl_placer *p, mpw_bytes data, uint64_t now) {
    mpl_acts a = {0};
    if (!p->has_follow_inflight) goto done;
    p->has_follow_inflight = false;
    uint32_t frame = p->follow_inflight;
    if (mpl_role(p) != MPM_ROLE_FOLLOWER) goto done;
    uint32_t parts = mpl_my_parts(p);
    compose(p, frame, parts, p->me, data.b, data.n, now, &a);
    push(&a, (mpl_act){.kind = MPL_SEND, .dest = to_mask(p->members & mpm_bits(p->n) & ~(1u << p->me)), .bytes = frame_bytes(p, frame, parts, NULL, 0, data.b, data.n)});
    if (!p->paused) try_follow(p, now, &a);
done:
    mpw_bytes_free(&data);
    return a;
}

void mpl_follow_lost(mpl_placer *p) { p->has_follow_inflight = false; }

mpl_acts mpl_peer_frame(mpl_placer *p, mpw_frame m, uint64_t now) {
    mpl_acts a = {0};
    /* The sender must render exactly these parts in our mode; the state comes from the authority only. */
    uint32_t theirs = mpm_parts_of(&p->mode, m.sys, p->spec.fanout);
    uint32_t auth = mpm_input_to(&p->mode, p->me);
    bool valid = m.sys != p->me && theirs && m.parts == theirs && (m.nstate == 0 || (m.sys == auth && m.nstate == mps_state_bytes(&p->spec)));
    if (!valid) {
        p->stats.ignored++;
        mpw_frame_free(&m);
        return a;
    }
    mpm_role r = mpl_role(p);
    if (r == MPM_ROLE_MIRROR) {
        if (m.nstate) {
            mpw_bytes st = {0};
            mpw_put(&st, m.state, m.nstate);
            push(&a, (mpl_act){.kind = MPL_WRITE_STATE, .bytes = st});
        }
        compose(p, m.frame, m.parts, m.sys, m.data, m.ndata, now, &a);
    } else if (r == MPM_ROLE_FOLLOWER && m.sys == auth) {
        if (!m.nstate) {
            p->stats.ignored++;
            mpw_frame_free(&m);
            return a;
        }
        /* The newest wins: a frame still waiting is replaced (the owner is ahead). */
        if (p->has_follow_next) {
            p->stats.ignored++;
            mpw_frame_free(&p->follow_next);
        }
        p->follow_next = m;
        p->has_follow_next = true;
        m = (mpw_frame){0};
        if (!p->paused) try_follow(p, now, &a);
    } else if (r == MPM_ROLE_FOLLOWER || r == MPM_ROLE_OWNER) {
        /* Another follower's parts (to the owner, or to a fellow follower). */
        if (!compose(p, m.frame, m.parts, m.sys, m.data, m.ndata, now, &a) && r == MPM_ROLE_OWNER && p->composer.n == 0) p->stats.ignored++;
    } else p->stats.ignored++;
    mpw_frame_free(&m);
    return a;
}

mpl_acts mpl_drop_all(mpl_placer *p, const char *why) {
    mpl_acts a = {0};
    if (p->has_change) {
        p->has_change = false;
        answer(p, p->change.asker, false, mt_strdup(why), &a);
        change_free(&p->change);
    }
    return a;
}
