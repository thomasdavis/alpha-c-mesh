/* peer_sched.rs: the fleet thread's scheduler glue: the member model from F_JOIN, F_LOADREP and F_PERF;
 * the leader's placement passes, rebalancing and migrations (mesh-sched-* decides, this commits); the
 * view transfer of a migration (F_VIEW_REQ / F_VIEW).
 *
 * A migration of programs from member A to B (state):
 * 1. the leader commits them placed on B with `from` A (A keeps them);
 * 2. B's reconciler loads them stopped and asks A for each view, producers first; A checks the document
 *    names that move, stops the program and sends the view (F_VIEW, 1 MiB chunks);
 * 3. B writes each view, starts them, consumers first, and reports the gap;
 * 4. the leader sees B's outcome and commits `from` cleared: A unloads them. B failed, or lost: the
 *    leader puts them back on A. A lost: B starts them fresh, loudly. */
#include "relay.h"
#include "mesh/abi/backend.h"
#include "mesh/abi/host_load.h"
#include "mesh/abi/host_mload.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/abi/view_heap.h"
#include "mesh/fleet_groups.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VIEW_WAIT_MS 30000u
#define MIG_WAIT_MS 90000u    /* a migration not done by then is put back */
#define PLACE_MS 250u         /* the placement pass's period on the leader */
#define PERF_KEEP_MS 600000u  /* measured performance is remembered this long after its member stopped reporting it */
#define FAIL_KEEP_MS 300000u  /* a program that failed on a member is not placed there again for this long */

static uint32_t host_u32_at(mpr_relay *r, size_t off) {
    uint32_t v;
    r->h.host_read(r->h.ctx, off, (uint8_t *)&v, 4);
    return v;
}

static float host_f32_at(mpr_relay *r, size_t off) {
    float v;
    r->h.host_read(r->h.ctx, off, (uint8_t *)&v, 4);
    return v;
}

static uint64_t host_u64_at(mpr_relay *r, size_t off) {
    uint64_t v;
    r->h.host_read(r->h.ctx, off, (uint8_t *)&v, 8);
    return v;
}

void sched_members_free(msm_member *ms, size_t n) {
    for (size_t i = 0; i < n; i++) msm_member_free(&ms[i]);
    free(ms);
}

static void fail_mem_set(mpr_relay *r, const char *name, uint32_t k, uint64_t hash, const char *why, bool uns, uint64_t at) {
    for (size_t i = 0; i < r->nfail_mem; i++)
        if (r->fail_mem[i].k == k && !strcmp(r->fail_mem[i].name, name)) {
            r->fail_mem[i].hash = hash;
            mt_set(&r->fail_mem[i].why, why);
            r->fail_mem[i].unsupported = uns;
            r->fail_mem[i].at = at;
            return;
        }
    r->fail_mem = mt_realloc(r->fail_mem, (r->nfail_mem + 1) * sizeof *r->fail_mem);
    r->fail_mem[r->nfail_mem++] = (fail_mem_t){mt_strdup(name), k, hash, mt_strdup(why), uns, at};
}

msm_member *sched_members(mpr_relay *r, uint64_t now, size_t *n) {
    uint32_t alive = mfc_alive(r->core, now);
    for (uint32_t k = 0; k < r->n; k++) {
        bool up = alive >> k & 1;
        if (up) r->down[k] = false;
        else if (!r->down[k]) r->down[k] = true, r->down_since[k] = now;
    }
    const mfd_doc *doc = mfc_committed(r->core);
    uint32_t cordon = doc->cordon;
    uint64_t hs = r->h.uptime_ms(r->h.ctx);
    board_t *b = &r->board;
    pthread_mutex_lock(&b->mu);
    /* Failures: what the members report now, remembered a while (at the program's current image). */
    for (uint32_t k = 0; k < r->n; k++)
        if (b->has[k])
            for (size_t i = 0; i < b->rep[k].nfails; i++) {
                const msr_fail *f = &b->rep[k].fails[i];
                fail_mem_set(r, f->name, k, f->hash, f->why, f->unsupported, now);
            }
    for (size_t i = r->nfail_mem; i-- > 0;) {
        fail_mem_t *f = &r->fail_mem[i];
        const mfd_prog *p = mfd_prog_get(doc, f->name);
        if (p && p->hash == f->hash && (f->unsupported || now - f->at < FAIL_KEEP_MS)) continue;
        free(f->name), free(f->why);
        r->fail_mem[i] = r->fail_mem[--r->nfail_mem];
    }
    msm_member *ms = mt_alloc(r->n * sizeof *ms);
    for (uint32_t k = 0; k < r->n; k++) {
        msm_member m = msm_member_default();
        m.id = k;
        m.up = alive >> k & 1;
        m.down_ms = r->down[k] ? now - r->down_since[k] : 0;
        m.cordoned = cordon >> k & 1;
        const mpk_caps *c = k == r->me ? &r->my_caps : r->has_caps[k] ? &r->caps[k] : NULL;
        uint32_t caps_kind = c ? c->backend : 0;
        if (c) {
            MSM_SET(m.host, c->host);
            uint32_t kind = mesh_backend_kind(c->backend);
            MSM_SET(m.gpu, kind < MESH_BACKENDS_LEN ? MESH_BACKENDS[kind] : "?");
            MSM_SET(m.arch, c->arch);
            m.cc = c->cc, m.sms = c->sms, m.blocks_per_sm = c->blocks_per_sm, m.max_threads = c->max_threads, m.max_shared = c->max_shared;
        }
        if (b->has[k]) {
            const msr_report *rp = &b->rep[k];
            m.cpus = rp->cpus, m.cores_big = rp->big, m.cores_little = rp->little, m.ram_mb = rp->ram_mb, m.disk_free_mb = rp->disk_free_mb;
            m.heap_kb = rp->heap_kb, m.slots_free = rp->slots_free;
            if (k != r->me) {
                m.nrtt = r->n;
                for (uint32_t j = 0; j < r->n; j++) {
                    m.rtt_us[j] = mof32_none();
                    for (size_t i = 0; i < rp->nrtt; i++)
                        if (rp->rtt[i].k == j) {
                            m.rtt_us[j] = mof32_some(rp->rtt[i].us);
                            break;
                        }
                }
            }
        }
        if (k == r->me) {
            m.nrtt = r->n;
            for (uint32_t j = 0; j < r->n; j++) m.rtt_us[j] = b->rtt_us[j];
        }
        for (size_t i = 0; i < r->nfail_mem; i++)
            if (r->fail_mem[i].k == k) {
                char *w = r->fail_mem[i].unsupported ? mt_fmt("unsupported (%s)", r->fail_mem[i].why) : mt_strdup(r->fail_mem[i].why);
                ms_set(&m.fails, r->fail_mem[i].name, w, true);
                free(w);
            }
        size_t l = mesh_mload(k);
        uint64_t at = host_u64_at(r, l + MESH_ML_AT_MS);
        if (at != 0 && (hs > at ? hs - at : 0) < 10000) {
            uint32_t lf = host_u32_at(r, l + MESH_ML_FLAGS);
            m.load_known = true;
            /* Metal's figure is submission overhead and queueing, not time slicing: unknown to the scheduler. */
            m.gpu_share = (lf & MESH_LF_GPU) && mesh_backend_kind(caps_kind) != MESH_BACKEND_METAL ? mof32_some(host_f32_at(r, l + MESH_ML_SHARE10)) : mof32_none();
            m.cpu_util = host_f32_at(r, l + MESH_ML_CPU);
            m.cpu_big = host_f32_at(r, l + MESH_ML_CPU_BIG);
            m.cpu_little = host_f32_at(r, l + MESH_ML_CPU_LITTLE);
            m.psi_cpu = lf & MESH_LF_PSI ? mof32_some(host_f32_at(r, l + MESH_ML_PSI_CPU)) : mof32_none();
            m.psi_mem = lf & MESH_LF_PSI ? mof32_some(host_f32_at(r, l + MESH_ML_PSI_MEM)) : mof32_none();
            m.meshd_cpu = host_f32_at(r, l + MESH_ML_MESHD_CPU);
            /* Smoothed: the scheduler reads the averages. */
            if (!r->has_load[k]) {
                r->has_load[k] = true;
                r->load_avg[k] = (mpv_load){0, m.cpu_util, m.psi_cpu, m.psi_mem, m.gpu_share};
            }
            mpv_load_add(&r->load_avg[k], at, m.cpu_util, m.psi_cpu, m.psi_mem, m.gpu_share);
            m.cpu_util = r->load_avg[k].cpu, m.psi_cpu = r->load_avg[k].psi_cpu, m.psi_mem = r->load_avg[k].psi_mem, m.gpu_share = r->load_avg[k].share;
            m.mem_avail_mb = host_u32_at(r, l + MESH_ML_MEM_AVAIL_MB);
            if (m.ram_mb == 0) m.ram_mb = host_u32_at(r, l + MESH_ML_MEM_TOTAL_MB);
            if (m.disk_free_mb == 0) m.disk_free_mb = host_u32_at(r, l + MESH_ML_DISK_FREE_MB);
        }
        ms[k] = m;
    }
    pthread_mutex_unlock(&b->mu);
    *n = r->n;
    return ms;
}

static perf_mem_t *perf_mem_get(mpr_relay *r, const char *name, uint32_t k) {
    for (size_t i = 0; i < r->nperf_mem; i++)
        if (r->perf_mem[i].k == k && !strcmp(r->perf_mem[i].name, name)) return &r->perf_mem[i];
    return NULL;
}

msm_perfs sched_perf_map(mpr_relay *r, uint64_t now) {
    board_t *b = &r->board;
    uint64_t t = mpr_mono_us();
    pthread_mutex_lock(&b->mu);
    for (uint32_t k = 0; k < r->n; k++) {
        if (!b->has[k] || t - b->at_us[k] > 10000000) continue;
        for (size_t i = 0; i < b->rep[k].nprogs; i++) {
            perf_mem_t *pm = perf_mem_get(r, b->rep[k].progs[i].name, k);
            if (!pm) {
                r->perf_mem = mt_realloc(r->perf_mem, (r->nperf_mem + 1) * sizeof *r->perf_mem);
                pm = &r->perf_mem[r->nperf_mem++];
                pm->name = mt_strdup(b->rep[k].progs[i].name);
                pm->k = k;
            }
            pm->perf = b->rep[k].progs[i].perf;
            pm->at = now;
        }
    }
    for (size_t i = r->nperf_mem; i-- > 0;)
        if (now - r->perf_mem[i].at >= PERF_KEEP_MS) {
            free(r->perf_mem[i].name);
            r->perf_mem[i] = r->perf_mem[--r->nperf_mem];
        }
    msm_perfs m = {0};
    for (size_t i = 0; i < r->nperf_mem; i++) {
        msm_perf p = r->perf_mem[i].perf;
        uint32_t k = r->perf_mem[i].k;
        /* A program not reported by a member that reports (it left there): not running there now. */
        if (b->has[k] && t - b->at_us[k] < 10000000) {
            bool listed = false;
            for (size_t j = 0; j < b->rep[k].nprogs; j++) listed |= !strcmp(b->rep[k].progs[j].name, r->perf_mem[i].name);
            if (!listed) p.run = false;
        }
        msm_perfs_set(&m, r->perf_mem[i].name, k, &p);
    }
    pthread_mutex_unlock(&b->mu);
    return m;
}

mfd_doc sched_pass(mpr_relay *r, const mfd_doc *doc, uint64_t now, mt_strs *changes) {
    size_t n;
    msm_member *ms = sched_members(r, now, &n);
    mst_owner(r->tracker, ms, n, now, &r->w);
    msm_perfs perf = sched_perf_map(r, now);
    msp_input in = {doc, ms, n, &perf, &r->w};
    msp_explains_free(&r->explains);
    mfd_doc d = msp_place(&in, changes, &r->explains);
    msm_perfs_free(&perf);
    sched_members_free(ms, n);
    return d;
}

mfd_doc sched_edit(mpr_relay *r, const mfd_doc *base, mfd_doc doc, uint64_t now, mt_strs *changes) {
    mfd_doc d = sched_pass(r, &doc, now, changes);
    mfd_doc_free(&doc);
    size_t n;
    msm_member *ms = sched_members(r, now, &n);
    msm_perfs perf = sched_perf_map(r, now);
    for (size_t i = 0; i < d.ngroups; i++) {
        mfd_group *g = &d.groups[i];
        bool fresh = true;
        for (size_t j = 0; j < base->ngroups; j++)
            if (!strcmp(base->groups[j].name, g->name) && base->groups[j].epoch == g->epoch) fresh = false;
        if (!fresh) continue;
        const mfd_prog *p = mfd_prog_get(&d, g->program);
        if (!p || !(p->needs.group_size > 0 || p->needs.ngroup_members)) continue;
        msp_input in = {&d, ms, n, &perf, &r->w};
        ml_list members;
        char *why;
        if (msp_group_choice(&in, g, &members, &why)) {
            if (!ml_eq(&members, &g->members)) {
                char *l = ml_text(&members);
                mt_push_own(changes, mt_fmt("group %s epoch %u members %s (%s)", g->name, g->epoch, l, why));
                free(l);
                ml_free(&g->members);
                g->members = members;
            } else ml_free(&members);
            free(why);
        }
    }
    msm_perfs_free(&perf);
    sched_members_free(ms, n);
    return d;
}

void sched_log_decision(mpr_relay *r, const char *what) {
    uint64_t t = r->h.uptime_ms(r->h.ctx);
    if (r->sched_log.n >= 16) mt_strs_remove(&r->sched_log, 0);
    mt_push_own(&r->sched_log, mt_fmt("%.1f s: %s", (double)t / 1e3, what));
}

static void start_migration(mpr_relay *r, const mfd_doc *doc, mst_move mv, uint64_t now) {
    uint64_t since = mfc_next_version(r->core);
    mfd_doc d = mst_start_move(doc, &mv, since);
    char *progs = mt_join(&mv.programs, ",");
    char *what = mt_fmt("sched: %s %s from system %u to %u%s: %s", mv.state ? "migrate" : "restart", progs, mv.from, mv.to, mv.forced ? " (pinned)" : "", mv.why);
    free(progs);
    relay_log(r, false, mt_strdup(what));
    sched_log_decision(r, what);
    mst_moved(r->tracker, &mv, now);
    uint64_t v;
    if (relay_propose(r, d, what, (replyto){0}, &v)) {
        if (r->has_mig) mst_move_free(&r->mig.mv);
        r->has_mig = true;
        r->mig = (mig_t){mv, v, mpr_mono_ms()};
    } else mst_move_free(&mv);
}

/* The migration in flight: the target's outcome (done: release the source), failed or lost (put it
 * back), the source lost (the target starts fresh). */
static void migration_check(mpr_relay *r, const mfd_doc *doc, uint64_t now) {
    if (!r->has_mig) return;
    mst_move *mv = &r->mig.mv;
    uint64_t v = r->mig.version;
    /* Not committed (the proposal failed): nothing moved. */
    if (doc->version < v) {
        if (!r->has_pending) {
            char *p = mt_join(&mv->programs, ",");
            relay_log(r, true, mt_fmt("sched: the migration of %s was not committed; dropped", p));
            free(p);
            mst_move_free(mv);
            r->has_mig = false;
        }
        return;
    }
    bool any_from = false;
    for (size_t i = 0; i < mv->programs.n; i++) {
        const mfd_prog *p = mfd_prog_get(doc, mv->programs.v[i]);
        any_from |= p && p->has_from;
    }
    if (!any_from && mv->state) {
        mst_move_free(mv);
        r->has_mig = false;
        return;
    }
    uint32_t alive = mfc_alive(r->core, now);
    /* The target's outcome for each moved program (its reconcile status as a whole is not the move's). */
    mst_outcome *outs;
    size_t nouts;
    char *reason;
    mpr_recon rs = {0};
    if (mv->to == r->me) {
        r->h.recon(r->h.ctx, &rs);
        outs = rs.moves, nouts = rs.nmoves, reason = rs.reason ? rs.reason : "";
    } else outs = r->st[mv->to].moves, nouts = r->st[mv->to].nmoves, reason = r->st[mv->to].reason ? r->st[mv->to].reason : "";
    bool ok;
    char *text;
    double gap;
    bool decided = mst_mig_verdict(mv, v, alive >> mv->to & 1, outs, nouts, reason, mpr_mono_ms() - r->mig.t0_ms, MIG_WAIT_MS, &ok, &text, &gap);
    mpr_recon_free(&rs);
    if (!decided) return;
    char *names = mt_join(&mv->programs, ",");
    bool source_up = alive >> mv->from & 1;
    double secs = (double)(mpr_mono_ms() - r->mig.t0_ms) / 1e3;
    mfd_doc d;
    char *what;
    if (ok) {
        r->moves_done++;
        if (gap >= 0) r->last_gap_us = (uint32_t)(gap * 1e3);
        char *g = gap >= 0 ? mt_fmt("%.1f ms", gap) : mt_strdup("-");
        char *why = mt_fmt("moved from system %u (%s; gap %s)", mv->from, mv->why, g);
        d = mst_end_move(doc, mv, true, why);
        char *gs = gap >= 0 ? mt_fmt(" (gap %.1f ms)", gap) : mt_strdup("");
        what = mt_fmt("sched: %s moved from system %u to %u in %.2f s%s: the source releases it", names, mv->from, mv->to, secs, gs);
        free(g), free(why), free(gs);
    } else if (source_up) {
        relay_log(r, true, mt_fmt("sched: the migration of %s from system %u to %u failed (%s); putting it back on system %u", names, mv->from, mv->to, text, mv->from));
        char *why = mt_fmt("a move to system %u failed (%s): back", mv->to, text);
        d = mst_end_move(doc, mv, false, why);
        what = mt_fmt("sched: %s back on system %u (%s)", names, mv->from, text);
        free(why);
    } else {
        relay_log(r, true, mt_fmt("sched: the source of %s, system %u, was lost during its migration (%s): system %u starts it fresh (its view is lost)", names,
                                  mv->from, text, mv->to));
        char *why = mt_fmt("the source, system %u, was lost during the move: started fresh (OP_INIT)", mv->from);
        d = mst_end_move(doc, mv, true, why);
        what = mt_fmt("sched: %s on system %u without its view (the source was lost)", names, mv->to);
        free(why);
    }
    free(text), free(names);
    relay_log(r, false, mt_strdup(what));
    sched_log_decision(r, what);
    mst_move_free(mv);
    r->has_mig = false;
    relay_propose(r, d, what, (replyto){0}, NULL);
}

void sched_schedule(mpr_relay *r, uint64_t now) {
    if (now - r->last_sched < PLACE_MS) return;
    r->last_sched = now;
    mfd_doc doc = mfd_doc_clone(mfc_committed(r->core));
    if (doc.version == 0) goto done;
    /* A migration the document has in flight that this leader does not track: taken over. */
    if (!r->has_mig) {
        mst_resumed res = mst_resume_moves(&doc);
        if (res.n) {
            char *p = mt_join(&res.moves[0].programs, ",");
            char *what = mt_fmt("sched: takes over the migration of %s from system %u to %u (version %llu)", p, res.moves[0].from, res.moves[0].to,
                                (unsigned long long)res.since[0]);
            relay_log(r, false, mt_strdup(what));
            sched_log_decision(r, what);
            free(p), free(what);
            r->has_mig = true;
            r->mig = (mig_t){mst_move_clone(&res.moves[0]), res.since[0], mpr_mono_ms()};
        }
        mst_resumed_free(&res);
    }
    /* A migration in flight: done, failed, or waiting. */
    if (r->has_mig) {
        migration_check(r, &doc, now);
        if (r->has_pending) goto done;
    }
    /* Placement: new programs, members gone or no longer fitting (not while paused). */
    mt_strs ch;
    mfd_doc d = sched_pass(r, &doc, now, &ch);
    if (ch.n && !doc.sched_paused) {
        char *j = mt_join(&ch, "; "), *what = mt_fmt("sched: %s", j);
        free(j);
        sched_log_decision(r, what);
        mt_strs_free(&ch);
        relay_propose(r, d, what, (replyto){0}, NULL);
        goto done;
    }
    mt_strs_free(&ch);
    mfd_doc_free(&d);
    if (now - r->last_rebalance < r->w.tick_ms) goto done;
    r->last_rebalance = now;
    size_t n;
    msm_member *ms = sched_members(r, now, &n);
    mst_owner(r->tracker, ms, n, now, &r->w);
    msm_perfs perf = sched_perf_map(r, now);
    msp_input in = {&doc, ms, n, &perf, &r->w};
    mst_move mv;
    if (mst_step(r->tracker, &in, now, r->has_mig, &mv)) {
        start_migration(r, &doc, mv, now);
        goto free_model;
    }
    /* The lockstep group: a re-form proposed (committed with autoreform). */
    char *name, *why;
    ml_list members;
    if (mst_group_step(r->tracker, &in, now, &name, &members, &why)) {
        char *l = ml_text(&members), *line = mt_fmt("sched: proposes re-forming group %s over %s: %s", name, l, why);
        free(l);
        if (doc.autoreform && !doc.sched_paused) {
            mfd_doc nd = mfd_doc_clone(&doc);
            mfd_group *g = mfd_group_named(&nd, name);
            if (g) mfg_new_epoch(g, ml_clone(&members));
            char *logged = mt_fmt("%s (autoreform: committing)", line);
            sched_log_decision(r, logged);
            free(logged);
            free(r->group_prop);
            r->group_prop = NULL;
            relay_propose(r, nd, line, (replyto){0}, NULL);
        } else if (!mt_eq(r->group_prop, line)) {
            /* Logged when the proposed set changes, not when only its scores move. */
            const char *a = r->group_prop ? strstr(r->group_prop, ": ") : NULL, *b = strstr(line, ": ");
            const char *ae = a ? strstr(a + 2, ": ") : NULL, *be = b ? strstr(b + 2, ": ") : NULL;
            size_t al = a ? (ae ? (size_t)(ae - a) : strlen(a)) : 0, bl = b ? (be ? (size_t)(be - b) : strlen(b)) : 0;
            if (!a || !b || al != bl || strncmp(a, b, al) != 0) {
                relay_log(r, false, mt_fmt("%s (`group %s reform` or `sched autoreform on`)", line, name));
                sched_log_decision(r, line);
            }
            free(r->group_prop);
            r->group_prop = line;
            line = NULL;
        }
        free(line), free(name), free(why);
        ml_free(&members);
    } else {
        free(r->group_prop);
        r->group_prop = NULL;
    }
free_model:
    msm_perfs_free(&perf);
    sched_members_free(ms, n);
done:
    mfd_doc_free(&doc);
}

/* ---- the view transfer */

typedef struct {
    mpr_relay *r;
    uint32_t k, id;
    char *name;
} view_job;

static void send_view_frame(mpr_relay *r, uint32_t k, uint32_t id, uint32_t ok, uint32_t abi, uint32_t total, uint32_t off, const uint8_t *b, size_t n, bool wait) {
    mpw_bytes f = {0};
    uint8_t t = MESH_F_VIEW;
    mpw_put(&f, &t, 1);
    mpw_put32(&f, id);
    mpw_put32(&f, ok);
    mpw_put32(&f, abi);
    mpw_put32(&f, total);
    mpw_put32(&f, off);
    mpw_put32(&f, (uint32_t)n);
    mpw_put(&f, b, n);
    /* A full link queue: wait for room (a view is a few frames). */
    uint64_t t0 = mpr_mono_ms();
    while (!mpr_send(r, k, f.b, f.n, false)) {
        if (!wait || mpr_mono_ms() - t0 > 10000) {
            if (wait) relay_log(r, true, mt_fmt("sched: a view for system %u: the link's queue stayed full", k));
            break;
        }
        struct timespec ts = {0, 2000000};
        nanosleep(&ts, NULL);
    }
    mpw_bytes_free(&f);
}

static void view_fail(mpr_relay *r, uint32_t k, uint32_t id, const char *e) {
    size_t n = strlen(e) > MESH_F_REASON_MAX ? MESH_F_REASON_MAX : strlen(e);
    send_view_frame(r, k, id, 0, 0, 0, 0, (const uint8_t *)e, n, false);
}

static void *view_out(void *arg) {
    view_job *j = arg;
    mpr_relay *r = j->r;
    uint64_t t0 = mpr_mono_us();
    uint32_t abi = 0;
    mpw_bytes v = {0};
    char *err = NULL;
    if (r->h.stop_and_view(r->h.ctx, j->name, &abi, &v, &err)) {
        double stop_ms = (double)(mpr_mono_us() - t0) / 1e3;
        size_t total = v.n, off = 0;
        do {
            size_t n = total - off < MESH_F_VIEW_CHUNK ? total - off : MESH_F_VIEW_CHUNK;
            send_view_frame(r, j->k, j->id, 1, abi, (uint32_t)total, (uint32_t)off, v.b + off, n, true);
            off += n;
        } while (off < total);
        relay_log(r, false, mt_fmt("sched: %s: stopped in %.1f ms, its view (%zu bytes) sent to system %u", j->name, stop_ms, total, j->k));
    } else {
        relay_log(r, true, mt_fmt("sched: %s's view for system %u: %s", j->name, j->k, err));
        char *e = mt_fmt("system %u: %s: %s", r->me, j->name, err);
        view_fail(r, j->k, j->id, e);
        free(e), free(err);
    }
    mpw_bytes_free(&v);
    free(j->name);
    free(j);
    return NULL;
}

/* The target asks for a view: authorised only by a document naming the move (committed or accepted). */
void sched_serve_view(mpr_relay *r, uint32_t k, uint32_t id, const char *name, uint64_t hash) {
    bool ok = false;
    const mfd_doc *docs[2] = {mfc_committed(r->core), mfc_accepted(r->core)};
    for (int i = 0; i < 2; i++) {
        const mfd_prog *p = mfd_prog_get(docs[i], name);
        if (p && p->hash == hash && p->has_from && p->from == r->me && p->at.some && ml_has(&p->at.l, k)) ok = true;
    }
    if (!ok) {
        char *e = mt_fmt("system %u: no desired state moves %s %016llx from here to system %u (not yet committed?)", r->me, name, (unsigned long long)hash, k);
        view_fail(r, k, id, e);
        free(e);
        return;
    }
    relay_log(r, false, mt_fmt("sched: system %u takes %s's view: stopping it here", k, name));
    view_job *j = mt_alloc(sizeof *j);
    *j = (view_job){r, k, id, mt_strdup(name)};
    pthread_t t;
    pthread_create(&t, NULL, view_out, j);
    pthread_detach(t);
}

void sched_fetch_view(mpr_relay *r, const char *name, uint64_t hash, uint32_t from, waiter *w) {
    if (!relay_linked(r, from)) {
        waiter_done(w, false, mt_fmt("system %u is not linked", from));
        return;
    }
    size_t slot = 0;
    while (slot < 32 && r->views[slot].used) slot++;
    if (slot == 32) {
        waiter_done(w, false, mt_strdup("too many views in flight"));
        return;
    }
    uint32_t id = r->next_id++;
    mpw_bytes f = {0};
    uint8_t t = MESH_F_VIEW_REQ, nm[MESH_NAME_BYTES], hb[8];
    mpw_put(&f, &t, 1);
    mpw_put32(&f, id);
    mpw_name16(name, nm);
    mpw_put(&f, nm, sizeof nm);
    memcpy(hb, &hash, 8);
    mpw_put(&f, hb, 8);
    relay_send(r, from, &f);
    mpw_bytes_free(&f);
    /* At most the program's view as the document gives it (else the whole view heap). */
    const mfd_prog *p = mfd_prog_get(mfc_committed(r->core), name);
    size_t vb = p ? p->needs.view_bytes : 0;
    size_t limit = vb > 0 ? vb : (size_t)MESH_VIEW_PAGES * MESH_VIEW_PAGE;
    r->views[slot] = (view_wait){true, id, w, mpv_wait_new(from, mpr_mono_ms() + VIEW_WAIT_MS, limit)};
}

void sched_view_chunk(mpr_relay *r, uint32_t id, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < 32; i++) {
        view_wait *vw = &r->views[i];
        if (!vw->used || vw->id != id) continue;
        uint32_t ok = mpw_u32_at(b, 4), abi = mpw_u32_at(b, 8), total = mpw_u32_at(b, 12), off = mpw_u32_at(b, 16), len = mpw_u32_at(b, 20);
        const uint8_t *body = b + MESH_VIEW_HEAD;
        if (MESH_VIEW_HEAD + (size_t)len > n) len = (uint32_t)(n - MESH_VIEW_HEAD);
        if (!ok) {
            vw->used = false;
            mpv_wait_free(&vw->v);
            waiter_done(vw->w, false, mt_strndup((const char *)body, len));
            return;
        }
        char *err = NULL;
        int t = mpv_take(&vw->v, total, off, body, len, &err);
        if (t == 0) return;
        vw->used = false;
        if (t == 1) {
            vw->w->view_abi = abi;
            mpw_put(&vw->w->view, vw->v.buf, vw->v.total);
            mpv_wait_free(&vw->v);
            waiter_done(vw->w, true, NULL);
        } else {
            mpv_wait_free(&vw->v);
            waiter_done(vw->w, false, err);
        }
        return;
    }
}

void sched_view_timeouts(mpr_relay *r, bool has_lost, uint32_t lost) {
    uint64_t t = mpr_mono_ms();
    for (size_t i = 0; i < 32; i++) {
        view_wait *vw = &r->views[i];
        if (!vw->used) continue;
        bool gone = has_lost && vw->v.from == lost;
        if (!gone && t < vw->v.deadline) continue;
        vw->used = false;
        uint32_t from = vw->v.from;
        mpv_wait_free(&vw->v);
        waiter_done(vw->w, false, gone ? mt_fmt("system %u was lost before it sent the view", from) : mt_fmt("system %u did not send the view in 30s", from));
    }
}
