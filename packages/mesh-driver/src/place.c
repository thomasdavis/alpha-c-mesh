/* Placement in the driver (driver.rs's placement section): a placeable program's Placer (the fleet
 * area's @ajax/mesh-placer) per slot when peered, its acts carried out on the mailbox and the peer
 * links, the mode's run state, and the commands and frames that reach it. */
#include "driver_int.h"

#include "mesh/place_compose.h"
#include "mesh/rsfmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t now_us(void) { return (uint64_t)mesh_mono_us(); }

static size_t ctl(uint32_t s, size_t off) { return mesh_prog_ctl(s) + off; }

const char *drv_role_word(mpm_role r) {
    switch (r) {
    case MPM_ROLE_LOCAL: return "Local";
    case MPM_ROLE_RUNNER: return "Runner";
    case MPM_ROLE_MIRROR: return "Mirror";
    case MPM_ROLE_OWNER: return "Owner";
    case MPM_ROLE_FOLLOWER: return "Follower";
    }
    return "?";
}

static bool local(const mpm_mode *m) { return m->kind == MPM_LOCAL; }

/* The committed placement request for name (FleetView's), as the placer takes it; NULL: none. */
static bool fleet_request(const mesh_driver *d, const char *name, mt_strs *out) {
    for (size_t i = 0; i < d->nfleet_places; i++) {
        const mesh_place_req *q = &d->fleet_places[i];
        if (strcmp(q->name, name)) continue;
        *out = (mt_strs){0};
        for (size_t j = 0; j < q->nwords; j++) mt_push(out, q->words[j]);
        return true;
    }
    return false;
}

static void strs_free(mt_strs *l) {
    for (size_t i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    *l = (mt_strs){0};
}

static void placer_log(void *ctx, const char *line) {
    (void)ctx;
    mesh_info("%s", line);
}

static void dplace_free(dplace *p) {
    if (!p) return;
    mpl_free(p->p);
    free(p);
}

/* Some(why) when slot s's run state belongs to its placement here (pin elsewhere, or split follower):
 * start/stop/reset are refused. */
char *drv_placed_here(const mesh_driver *d, uint32_t s) {
    const dplace *p = d->place[s];
    if (!p) return NULL;
    mpm_role r = mpl_role(p->p);
    if (r != MPM_ROLE_MIRROR && r != MPM_ROLE_FOLLOWER) return NULL;
    const char *name = drv_name(d->slots[s]);
    char *label = mpm_label(mpl_mode(p->p));
    char *why = mesh_buf_fmt("%s is placed (%s): system %u runs it; `place %s local` first, or change it there", name, label,
                             mpm_input_to(mpl_mode(p->p), d->me), name);
    free(label);
    return why;
}

static char *place_report(const mesh_driver *d, uint32_t s) {
    const dplace *p = d->place[s];
    const mpl_stats *st = mpl_stats_of(p->p);
    char *label = mpm_label(mpl_mode(p->p));
    char *r = mesh_buf_fmt("%s role %s system %u paused %s shown %llu lat_n %llu lat_sum_us %llu lat_max_us %u ignored %llu part_ns %u uptime_ms %llu", label,
                           drv_role_word(mpl_role(p->p)), d->me, mpl_paused(p->p) ? "true" : "false", (unsigned long long)st->shown,
                           (unsigned long long)st->lat_n, (unsigned long long)st->lat_sum_us, st->lat_max_us, (unsigned long long)st->ignored, p->part_ns,
                           (unsigned long long)mesh_host_block_uptime_ms(&d->sh->host));
    free(label);
    return r;
}

/* PX_PLACE (the table extension) and the show header's mode words. */
void drv_place_words(mesh_driver *d, uint32_t s) {
    const dplace *p = d->place[s];
    const dslot *x = d->slots[s];
    if (!p || !x) return;
    mesh_mailbox mb = d->sh->mb;
    uint32_t m, sys;
    mpm_code(mpl_mode(p->p), &m, &sys);
    mesh_mailbox_set(mb, mesh_prog_ext(s) + MESH_PX_PLACE, m);
    mesh_mailbox_set(mb, mesh_prog_ext(s) + MESH_PX_PLACE_SYS, sys);
    uint8_t a[MESH_PLACE_PARTS_MAX];
    mpm_assign_bytes(mpl_mode(p->p), a);
    mesh_mailbox_write(mb, mesh_prog_ext(s) + MESH_PX_PLACE_ASSIGN, a, sizeof a, sizeof a);
    size_t show = (size_t)x->view_off + mpl_spec(p->p)->show;
    mesh_mailbox_set(mb, show + MESH_SH_MODE, m);
    mesh_mailbox_publish(mb, show + MESH_SH_SYS, sys);
}

typedef struct {
    mesh_mailbox mb;
    size_t view;
} vio;

static void vread(void *c, uint32_t off, uint32_t len, uint8_t *out) {
    vio *v = c;
    mesh_mailbox_read(v->mb, v->view + off, out, len);
}
static void vwrite(void *c, uint32_t off, const uint8_t *b, uint32_t len) {
    vio *v = c;
    mesh_mailbox_write(v->mb, v->view + off, b, len, len);
}
/* The mode changed here: the run state (a mirror's kernel stops; every other placed role runs), the
 * published words, the cadence. */
static void apply_mode(mesh_driver *d, uint32_t s, const mpm_mode *m) {
    dplace *p = d->place[s];
    dslot *x = d->slots[s];
    if (!p || !x) return;
    mpm_role role = mpm_role_of(m, d->me);
    p->has_follow = false;
    if (!mpm_runs(role)) {
        if (x->want_run) x->want_run = false, p->stopped = true;
    } else if (role != MPM_ROLE_LOCAL || p->stopped) {
        x->want_run = true;
        p->stopped = false;
    }
    mesh_mailbox_set(d->sh->mb, ctl(s, MESH_P_RUN), x->want_run);
    x->last_auto = mesh_mono_ns();
    char *label = mpm_label(m);
    mesh_info("place %s: %s here (system %u: %s)%s", drv_name(x), label, d->me, drv_role_word(role), x->want_run ? "" : "; its kernel stops here");
    free(label);
    drv_place_words(d, s);
    d->state_dirty = true;
}

static void dest_text(const mpl_dest *dst, char *out, size_t cap) { snprintf(out, cap, dst->to_mask ? "Mask(%u)" : "To(%u)", dst->k); }

/* Carries out a placer's acts for slot s (has_s false: the slot is gone). Takes acts. */
void drv_place_acts(mesh_driver *d, bool has_s, uint32_t s, mpl_acts acts) {
    mesh_mailbox mb = d->sh->mb;
    for (size_t i = 0; i < acts.n; i++) {
        mpl_act *a = &acts.v[i];
        switch (a->kind) {
        case MPL_SEND: {
            bool ok = a->dest.to_mask ? mesh_shared_send_mask(d->sh, a->dest.k, a->bytes.b, a->bytes.n) : mesh_shared_send_peer(d->sh, a->dest.k, a->bytes.b, a->bytes.n);
            if (!ok) {
                char t[32];
                dest_text(&a->dest, t, sizeof t);
                mesh_error("place: a frame for %s was dropped (no session, or its queue is full)", t);
            }
            break;
        }
        case MPL_ANSWER: {
            for (size_t j = 0; j < d->nplace_replies; j++) {
                if (d->place_replies[j].h != a->handle) continue;
                drv_answer(d->place_replies[j].reply, a->ok, a->text ? a->text : "");
                d->place_replies[j] = d->place_replies[--d->nplace_replies];
                break;
            }
            break;
        }
        case MPL_LOG: mesh_info("%s", a->text ? a->text : ""); break;
        case MPL_WRITE_STATE: {
            if (!has_s) break;
            dplace *p = d->place[s];
            dslot *x = d->slots[s];
            if (!p || !x) break;
            vio v = {mb, x->view_off};
            char *err = NULL;
            if (!mpc_write_state(mpl_spec(p->p), a->bytes.b, a->bytes.n, vwrite, &v, &err)) {
                mesh_error("place %s: %s", drv_name(x), err ? err : "");
                free(err);
                break;
            }
            /* A stopped copy now holds the whole state: it is initialised (no OP_INIT over it). */
            if (!drv_live(x)) x->needs_init = false;
            break;
        }
        case MPL_SHOW: {
            if (!has_s) break;
            dplace *p = d->place[s];
            dslot *x = d->slots[s];
            if (!p || !x) break;
            size_t show = (size_t)x->view_off + mpl_spec(p->p)->show;
            uint32_t seq = mesh_mailbox_get(mb, show + MESH_SH_SEQ) + 1;
            size_t k = seq & 1;
            mesh_mailbox_write(mb, show + MESH_SH_DATA + k * a->bytes.n, a->bytes.b, a->bytes.n, a->bytes.n);
            mesh_mailbox_set(mb, show + MESH_SH_BUF + MESH_SH_BUF_BYTES * k + MESH_SHB_FRAME, a->frame);
            mesh_mailbox_set(mb, show + MESH_SH_BUF + MESH_SH_BUF_BYTES * k + MESH_SHB_SYS, a->sys);
            mesh_mailbox_set(mb, show + MESH_SH_LAT_US, a->lat_us);
            uint32_t m, sys;
            mpm_code(mpl_mode(p->p), &m, &sys);
            mesh_mailbox_set(mb, show + MESH_SH_MODE, m);
            mesh_mailbox_set(mb, show + MESH_SH_SYS, sys);
            mesh_mailbox_publish(mb, show + MESH_SH_SEQ, seq);
            break;
        }
        case MPL_FOLLOW:
            if (has_s && d->place[s]) d->place[s]->has_follow = true, d->place[s]->follow = a->frame;
            break;
        case MPL_MODE:
            if (has_s) apply_mode(d, s, &a->mode);
            break;
        }
    }
    mpl_acts_free(&acts);
}

static bool spec_eq(const mps_spec *a, const mps_spec *b) {
    if (a->cap != b->cap || a->frame != b->frame || a->show != b->show || a->fanout != b->fanout || a->nstate != b->nstate || a->nout != b->nout) return false;
    for (size_t i = 0; i < a->nstate; i++)
        if (a->state[i].off != b->state[i].off || a->state[i].len != b->state[i].len) return false;
    for (size_t i = 0; i < a->nout; i++)
        if (a->out[i].off != b->out[i].off || a->out[i].len != b->out[i].len) return false;
    return true;
}

/* The manifest's placement as the placer takes it (its fanout the manifest's). */
static mps_spec spec_of(const mesh_manifest *m) {
    const mesh_manifest_place *p = m->place;
    mps_region st[p->nstate ? p->nstate : 1], out[p->nout ? p->nout : 1];
    for (size_t i = 0; i < p->nstate; i++) st[i] = (mps_region){p->state[i].off, p->state[i].len};
    for (size_t i = 0; i < p->nout; i++) out[i] = (mps_region){p->out[i].off, p->out[i].len};
    return mps_new(p->cap, st, p->nstate, p->frame, out, p->nout, p->show, m->fanout);
}

/* A placer for slot s's manifest (peered, `place` declared), or none. After an upgrade or rollback: kept
 * when the declaration is unchanged. */
void drv_sync_place(mesh_driver *d, uint32_t s) {
    dslot *x = d->slots[s];
    bool want = d->cfg.peered && x && x->image.manifest.place;
    mps_spec spec = {0};
    if (want) spec = spec_of(&x->image.manifest);
    dplace *old = d->place[s];
    if (old && want && spec_eq(mpl_spec(old->p), &spec)) {
        mps_free(&spec);
        drv_place_words(d, s);
        return;
    }
    if (!old && !want) return;
    if (old) {
        d->place[s] = NULL;
        char *why = mesh_buf_fmt("%s: its placement declaration changed", old->name);
        drv_place_acts(d, false, 0, mpl_drop_all(old->p, why));
        free(why);
        if (old->stopped && x) {
            x->want_run = true;
            mesh_mailbox_set(d->sh->mb, ctl(s, MESH_P_RUN), 1);
        }
        if (!local(mpl_mode(old->p))) mesh_info("place %s: its manifest's placement changed: local here", old->name);
        free(old->name);
        dplace_free(old);
    }
    if (want) {
        dplace *p = mesh_xcalloc(1, sizeof *p);
        p->name = mesh_xstrdup(drv_name(x));
        p->p = mpl_new(p->name, spec, d->me, d->n);
        mpl_set_log(p->p, placer_log, NULL);
        d->place[s] = p;
        mt_strs req;
        bool has = fleet_request(d, p->name, &req);
        mpl_acts acts = mpl_view(p->p, d->fleet_up, d->has_fleet_leader, d->fleet_leader, has ? &req : NULL, now_us());
        if (has) strs_free(&req);
        drv_place_acts(d, true, s, acts);
    }
    drv_place_words(d, s);
}

/* Unloaded: what waits on the placement is answered. */
void drv_place_drop(mesh_driver *d, uint32_t s, const char *name) {
    dplace *p = d->place[s];
    if (!p) return;
    d->place[s] = NULL;
    char *why = mesh_buf_fmt("%s was unloaded", name);
    drv_place_acts(d, false, 0, mpl_drop_all(p->p, why));
    free(why);
    free(p->name);
    dplace_free(p);
}

void drv_place_free_all(mesh_driver *d) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (!d->place[s]) continue;
        free(d->place[s]->name);
        dplace_free(d->place[s]);
        d->place[s] = NULL;
    }
    for (size_t i = 0; i < d->nplace_replies; i++) mesh_reply_drop(d->place_replies[i].reply);
    free(d->place_replies);
    d->place_replies = NULL;
    d->nplace_replies = d->cplace_replies = 0;
    for (size_t i = 0; i < d->nfleet_places; i++) {
        free(d->fleet_places[i].name);
        for (size_t j = 0; j < d->fleet_places[i].nwords; j++) free(d->fleet_places[i].words[j]);
        free(d->fleet_places[i].words);
    }
    free(d->fleet_places);
    d->fleet_places = NULL;
    d->nfleet_places = 0;
}

/* A placed program's frames hop between the machines: the driver ticks fast. */
bool drv_place_busy(const mesh_driver *d) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        const dplace *p = d->place[s];
        if (p && (!local(mpl_mode(p->p)) || mpl_busy(p->p))) return true;
    }
    return false;
}

/* The load sampler's mean busy time per task, for `split auto` (PL_TASK_US: the last known value while
 * the program ran no task). */
void drv_place_part_ns(mesh_driver *d) {
    mesh_host_block *h = &d->sh->host;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dplace *p = d->place[s];
        if (!p || !d->slots[s]) continue;
        size_t at = mesh_pload(s);
        if (!(mesh_host_block_get_u32(h, at + MESH_PL_FLAGS) & MESH_PLF_TASK)) continue;
        double ns = (double)mesh_host_block_get_f32(h, at + MESH_PL_TASK_US) * 1e3;
        p->part_ns = ns >= 4294967295.0 ? UINT32_MAX : ns > 0 ? (uint32_t)ns : 0;
        mpl_set_part_ns(p->p, p->part_ns);
    }
}

/* Every 10 s: each placed program's frames and their added latency. */
void drv_place_log(mesh_driver *d) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        const dplace *p = d->place[s];
        if (!p || local(mpl_mode(p->p))) continue;
        const mpl_stats *st = mpl_stats_of(p->p);
        char *label = mpm_label(mpl_mode(p->p));
        mesh_buf mean = MESH_BUF_INIT;
        mesh_rs_fixed(&mean, (double)st->lat_sum_us / (double)(st->lat_n ? st->lat_n : 1), 0);
        mesh_info("place %s: %s (%s): %llu frames shown, added latency mean %s us max %u us over %llu frames, %llu peer frames unused", p->name, label,
                  drv_role_word(mpl_role(p->p)), (unsigned long long)st->shown, mesh_buf_str(&mean), st->lat_max_us, (unsigned long long)st->lat_n,
                  (unsigned long long)st->ignored);
        mesh_buf_free(&mean);
        free(label);
    }
}

typedef struct {
    vio v;
    const mps_spec *spec;
} sio;

static mpw_bytes sstate(void *c) {
    sio *x = c;
    return mpc_read_state(x->spec, vread, &x->v);
}

/* Per tick: each placer's timeouts, pause acks and queued follow frames. */
void drv_place_tick(mesh_driver *d) {
    uint64_t now = now_us();
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dplace *p = d->place[s];
        dslot *x = d->slots[s];
        if (!p || !x) continue;
        bool drained = !drv_live(x) || mesh_pring_idle(&x->ring);
        sio io = {{d->sh->mb, x->view_off}, mpl_spec(p->p)};
        mpl_acts acts = mpl_tick(p->p, now, drained, sstate, &io);
        if (acts.n) drv_place_acts(d, true, s, acts);
        else mpl_acts_free(&acts);
    }
}

/* Slot s finished a batch of its own frame (Runner, Owner) or its follow batch. */
void drv_place_frame(mesh_driver *d, uint32_t s, bool own, bool follow) {
    dplace *p = d->place[s];
    dslot *x = d->slots[s];
    if (!p || !x) return;
    mesh_mailbox mb = d->sh->mb;
    const mps_spec *spec = mpl_spec(p->p);
    vio v = {mb, x->view_off};
    uint64_t now = now_us();
    uint32_t mask = mpl_my_parts(p->p);
    mpm_role role = mpl_role(p->p);
    if (own && mesh_pring_auto_in_flight(&x->ring) == 0 && (role == MPM_ROLE_RUNNER || role == MPM_ROLE_OWNER)) {
        uint32_t frame = mesh_mailbox_get(mb, (size_t)x->view_off + spec->frame);
        mpw_bytes st = mpc_read_state(spec, vread, &v), data = mpc_gather(spec, mask, vread, &v);
        drv_place_acts(d, true, s, mpl_own_frame(p->p, frame, st, data, now));
        p = d->place[s];
        x = d->slots[s];
        if (!p || !x) return;
    }
    if (follow && mesh_pring_follow_in_flight(&x->ring) == 0) {
        mpw_bytes data = mpc_gather(spec, mask, vread, &v);
        drv_place_acts(d, true, s, mpl_follow_done(p->p, data, now));
    }
}

void drv_place_follow_lost(mesh_driver *d, uint32_t s) {
    if (d->place[s]) mpl_follow_lost(d->place[s]->p);
}

/* The program's run state is held by its placement (inventory: a placement-stopped one counts as run). */
bool drv_place_stopped(const mesh_driver *d, const char *name) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++)
        if (d->place[s] && d->place[s]->stopped && !strcmp(d->place[s]->name, name)) return true;
    return false;
}

/* publish_slot's placement part: the follow batch (split, a follower's parts) on the owner's state; and
 * the role, whether it is paused, and its parts (an Owner publishes those only). */
void drv_place_publish(mesh_driver *d, uint32_t s, bool *paused, mpm_role *role, uint32_t *parts) {
    dplace *p = d->place[s];
    dslot *x = d->slots[s];
    *paused = false;
    *role = MPM_ROLE_LOCAL;
    *parts = 0;
    if (!p) return;
    *role = mpl_role(p->p);
    *paused = mpl_paused(p->p);
    *parts = mpl_my_parts(p->p);
    if (p->has_follow && !*paused && *role == MPM_ROLE_FOLLOWER) {
        uint32_t n = (uint32_t)__builtin_popcount(*parts);
        if (mesh_pring_room(&x->ring) >= n) {
            p->has_follow = false;
            for (uint32_t b = 0; b < 32; b++)
                if (*parts >> b & 1) mesh_pring_publish(&x->ring, d->sh->mb, MESH_OP_FOLLOW, b, mesh_kind_of(MESH_TK_FOLLOW), NULL);
        }
    }
}

/* ---- commands */

void drv_cmd_place(mesh_driver *d, mesh_cmd *c, mesh_reply *reply) {
    int s = drv_find(d, c->name);
    if (s < 0) {
        mesh_reply_errf(reply, "no program %s (`programs` lists them)", c->name);
        return;
    }
    dplace *p = d->place[s];
    if (!p && !d->cfg.peered) {
        mesh_reply_err(reply, "not peered: placement needs a cluster (--cluster, --system)");
        return;
    }
    if (!p) {
        mesh_reply_errf(reply, "%s declares no `place` in its manifest", c->name);
        return;
    }
    if (!c->has_words) {
        char *r = place_report(d, (uint32_t)s);
        mesh_reply_ok(reply, r);
        free(r);
        return;
    }
    mpm_target t;
    char *err = NULL;
    if (!mpl_parse(p->p, c->words, c->nwords, &t, &err)) {
        mesh_reply_err(reply, err ? err : "");
        free(err);
        return;
    }
    uint64_t h = ++d->place_handle;
    if (d->nplace_replies == d->cplace_replies) {
        d->cplace_replies = d->cplace_replies ? d->cplace_replies * 2 : 4;
        d->place_replies = realloc(d->place_replies, d->cplace_replies * sizeof *d->place_replies);
        if (!d->place_replies) abort();
    }
    d->place_replies[d->nplace_replies++] = (dplace_reply){h, reply};
    mt_strs w = {0};
    for (size_t i = 0; i < c->nwords; i++) mt_push(&w, c->words[i]);
    mpl_acts acts = mpl_request(p->p, &w, h, now_us());
    strs_free(&w);
    drv_place_acts(d, true, (uint32_t)s, acts);
}

void drv_cmd_peer_place(mesh_driver *d, mesh_cmd *c) {
    mpw_place *m = c->msg;
    int s = drv_find(d, m->name);
    if (s < 0 || !d->place[s]) {
        mesh_error("place: F_PLACE op %u for %s from system %u, which is not placeable here; ignored", m->op, m->name, c->from);
        return;
    }
    mpw_place taken = *m;
    memset(m, 0, sizeof *m);
    drv_place_acts(d, true, (uint32_t)s, mpl_peer_place(d->place[s]->p, taken, c->from, now_us()));
}

void drv_cmd_peer_frame(mesh_driver *d, mesh_cmd *c) {
    mpw_frame *m = c->msg;
    int s = drv_find(d, m->name);
    if (s < 0 || !d->place[s]) return;
    mpw_frame taken = *m;
    memset(m, 0, sizeof *m);
    drv_place_acts(d, true, (uint32_t)s, mpl_peer_frame(d->place[s]->p, taken, now_us()));
}

void drv_cmd_peer_input(mesh_driver *d, mesh_cmd *c) {
    int s = drv_find(d, c->name);
    if (s >= 0 && d->place[s] && mpm_input_to(mpl_mode(d->place[s]->p), d->me) == d->me) {
        char *text = NULL;
        if (drv_input(d, (uint32_t)s, c->bytes, c->len, &text) != 0) mesh_error("place: input from the peer: %s", text ? text : "");
        free(text);
        return;
    }
    mesh_error("place: %zu byte(s) of input for %s from a member, which does not apply its input here; dropped", c->len, c->name);
}

void drv_cmd_fleet_view(mesh_driver *d, mesh_cmd *c) {
    d->fleet_up = c->up;
    d->has_fleet_leader = c->has_leader;
    d->fleet_leader = c->leader;
    for (size_t i = 0; i < d->nfleet_places; i++) {
        free(d->fleet_places[i].name);
        for (size_t j = 0; j < d->fleet_places[i].nwords; j++) free(d->fleet_places[i].words[j]);
        free(d->fleet_places[i].words);
    }
    free(d->fleet_places);
    d->fleet_places = c->places;
    d->nfleet_places = c->nplaces;
    c->places = NULL;
    c->nplaces = 0;
    uint64_t now = now_us();
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dplace *p = d->place[s];
        if (!p) continue;
        mt_strs req;
        bool has = fleet_request(d, p->name, &req);
        mpl_acts acts = mpl_view(p->p, d->fleet_up, d->has_fleet_leader, d->fleet_leader, has ? &req : NULL, now);
        if (has) strs_free(&req);
        drv_place_acts(d, true, s, acts);
    }
}

/* Where slot s's input is applied: this machine, or the member its placement names. */
uint32_t drv_input_to(const mesh_driver *d, uint32_t s) { return d->place[s] ? mpm_input_to(mpl_mode(d->place[s]->p), d->me) : d->me; }
