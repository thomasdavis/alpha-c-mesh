/* peer.rs publish, fleet_text, groups_text, set_member, group_line, and peer_sched.rs sched_publish and
 * fleet_recs: what the fleet thread publishes (the status block's fleet, member and scheduler regions,
 * the `fleet`, `groups` and `sched` texts, and the fleet view's records in views.rs's text form). */
#include "relay.h"
#include "mesh/abi/group_state.h"
#include "mesh/abi/host_fleet.h"
#include "mesh/abi/host_members.h"
#include "mesh/abi/host_sched.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/reconcile.h"
#include "mesh/rust_fmt.h"
#include "mesh/sched_needs.h"
#include "mesh/text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUSPECT_NONE 0

static const char *gl_name(uint32_t s) { return s < MESH_GL_NAMES_LEN ? MESH_GL_NAMES[s] : "?"; }

/* fleet::members_of(mask) as sched::list writes it ("-" when empty). */
static char *mask_list(uint32_t mask) {
    ml_list l = ml_members(mask);
    char *s = ml_text(&l);
    ml_free(&l);
    return s;
}

/* The reconcile word: "pending" while behind (and not failed), else RS_NAMES. */
static const char *rs_word(uint64_t applied, uint64_t version, uint32_t st) {
    if (applied < version && st != MESH_RS_FAILED) return "pending";
    return st < MESH_RS_NAMES_LEN ? MESH_RS_NAMES[st] : "?";
}

/* ---- views.rs Rec, written straight into its text form */

static void rec(mt_buf *o, const char *kind) { mt_puts(o, kind); }

/* A field: its value one token (whitespace and '=' become '_', empty becomes '-'). */
static void kv(mt_buf *o, const char *k, const char *v) {
    mt_printf(o, " %s=", k);
    if (!v || !*v) {
        mt_putc(o, '-');
        return;
    }
    size_t n = strlen(v);
    for (size_t i = 0; i < n;) {
        size_t ws = mesh_utf8_ws_len(v + i, n - i);
        if (ws) {
            mt_putc(o, '_');
            i += ws;
        } else {
            mt_putc(o, v[i] == '=' ? '_' : v[i]);
            i++;
        }
    }
}

static void kv_own(mt_buf *o, const char *k, char *v) {
    kv(o, k, v);
    free(v);
}

static void kvu(mt_buf *o, const char *k, uint64_t v) { mt_printf(o, " %s=%llu", k, (unsigned long long)v); }
static void kvi(mt_buf *o, const char *k, int64_t v) { mt_printf(o, " %s=%lld", k, (long long)v); }
static void opt_u(mt_buf *o, const char *k, bool some, uint64_t v) {
    if (some) kvu(o, k, v);
    else kv(o, k, "-");
}

/* A float with p decimals ('-' when absent or not finite). */
static void kf(mt_buf *o, const char *k, bool some, double v, int p) {
    if (some && isfinite(v)) mt_printf(o, " %s=%.*f", k, p, v);
    else kv(o, k, "-");
}

/* The text (newlines become spaces) and the end of the record. */
static void rec_end(mt_buf *o, const char *text) {
    if (text && *text) {
        mt_puts(o, " -- ");
        for (const char *c = text; *c; c++) mt_putc(o, *c == '\n' || *c == '\r' ? ' ' : *c);
    }
    mt_putc(o, '\n');
}

/* ---- the status block */

void publish_member(mpr_relay *r, uint32_t k, uint32_t state, const mpk_caps *c) {
    const mpr_host *h = &r->h;
    size_t m = mesh_member(k);
    h->host_u32(h->ctx, m + MESH_M_STATE, state);
    if (!c) return;
    h->host_u32(h->ctx, m + MESH_M_CC, c->cc);
    h->host_u32(h->ctx, m + MESH_M_SMS, c->sms);
    h->host_u32(h->ctx, m + MESH_M_SMS_TOTAL, c->sms_total);
    h->host_u32(h->ctx, m + MESH_M_BLOCKS_PER_SM, c->blocks_per_sm);
    h->host_str(h->ctx, m + MESH_M_GPU, 32, c->gpu);
    h->host_str(h->ctx, m + MESH_M_ARCH, 16, c->arch);
    h->host_str(h->ctx, m + MESH_M_HOST, 32, c->host);
    h->host_u32(h->ctx, m + MESH_M_BACKEND, c->backend);
}

char *publish_group_line(const mfd_group *g) {
    char *l = ml_text(&g->members), *a = g->auto_ms > 0 ? mt_fmt(" auto_ms %u", g->auto_ms) : mt_strdup("");
    char *s = mt_fmt("group %s program %s epoch %u cycle %u members %s state %s on_loss %s%s", g->name, g->program, g->epoch, g->cycle, l,
                     g->state == MFD_RUNNING ? "running" : "paused", g->on_loss == MFD_LOSS_PAUSE ? "pause" : "reform", a);
    free(l);
    free(a);
    return s;
}

/* A member's link state as `fleet` and the view say it. */
static const char *link_word(mpr_relay *r, uint32_t k, uint32_t alive, uint32_t suspect) {
    if (!(alive >> k & 1)) return relay_linked(r, k) ? "linked" : "down";
    return suspect >> k & 1 ? "suspect" : "up";
}

/* `fleet` on the control port. */
static char *fleet_text(mpr_relay *r, uint64_t now, uint32_t alive, uint32_t suspect, const mpr_recon *rs) {
    const mfd_doc *doc = mfc_committed(r->core);
    const mfc_config *cfg = mfc_cfg(r->core);
    uint32_t leader;
    bool has_leader = mfc_leader(r->core, &leader);
    mt_buf s = {0};
    char *up = mask_list(alive), *sus = mask_list(suspect);
    char lw[16] = "-";
    if (has_leader) snprintf(lw, sizeof lw, "%u", leader);
    mt_printf(&s, "fleet system %u of %u role %s leader %s term %llu version %llu accepted %llu applied %llu quorum %u up %s suspect %s %s\n", r->me, r->n,
              mfc_role_word(mfc_role_of(r->core)), lw, (unsigned long long)mfc_term(r->core), (unsigned long long)doc->version,
              (unsigned long long)mfc_accepted(r->core)->version, (unsigned long long)rs->applied, mfc_quorum(cfg), up, sus,
              mfc_fixed(cfg) ? "fixed-coordinator" : mfc_has_quorum(r->core, now) ? "majority" : "no-majority");
    free(up);
    free(sus);
    uint64_t hs = r->h.uptime_ms(r->h.ctx);
    for (uint32_t k = 0; k < r->n; k++) {
        const char *state, *why, *host;
        uint64_t applied;
        uint32_t st;
        char heard[24] = "-";
        if (k == r->me) {
            state = "up", applied = rs->applied, st = rs->status, why = rs->reason ? rs->reason : "";
            snprintf(heard, sizeof heard, "0");
            host = r->my_caps.host;
        } else {
            const mpk_beat_status *b = &r->st[k];
            state = link_word(r, k, alive, suspect), applied = b->applied, st = b->status, why = b->reason ? b->reason : "";
            bool linked;
            uint64_t rx = relay_last_rx(r, k, &linked);
            if (linked) snprintf(heard, sizeof heard, "%llu", (unsigned long long)(hs > rx ? hs - rx : 0));
            host = r->has_caps[k] ? r->caps[k].host : "?";
        }
        mt_printf(&s, "member %u %s reconcile %s applied %llu heard_ms %s host %s%s", k, state, rs_word(applied, doc->version, st), (unsigned long long)applied,
                  heard, host, has_leader && leader == k ? " leader" : "");
        if (*why && strcmp(why, "ok") != 0) mt_printf(&s, st == MESH_RS_SYNC ? " last %s" : " reason %s", why);
        mt_putc(&s, '\n');
    }
    for (size_t i = 0; i < doc->nprogs; i++) {
        const mfd_prog *p = &doc->prog[i];
        char *at = ml_at_word(&p->at);
        mt_printf(&s, "program %s dir %s hash %016llx run %u%s", doc->pname[i], p->dir, (unsigned long long)p->hash, (unsigned)p->run, p->lockstep ? " lockstep" : "");
        if (p->has_prev) mt_printf(&s, " prev %s %016llx", p->prev_dir, (unsigned long long)p->prev_hash);
        mt_printf(&s, " at %s", at);
        free(at);
        if (p->pin.some) {
            char *l = ml_text(&p->pin.l);
            mt_printf(&s, " pin %s", l);
            free(l);
        }
        if (p->has_from) mt_printf(&s, " from %u", p->from);
        mt_putc(&s, '\n');
    }
    for (size_t i = 0; i < doc->ngroups; i++) {
        char *g = publish_group_line(&doc->groups[i]);
        mt_printf(&s, "%s\n", g);
        free(g);
    }
    for (size_t i = 0; i < doc->nplaces; i++) {
        char *w = mt_join(&doc->place_words[i], " ");
        mt_printf(&s, "place %s %s\n", doc->place_name[i], w);
        free(w);
    }
    return mt_take(&s);
}

/* `groups` on the control port. */
static char *groups_text(mpr_relay *r) {
    const mfd_doc *doc = mfc_committed(r->core);
    mt_buf s = {0};
    for (size_t i = 0; i < doc->ngroups; i++) {
        const mfd_group *g = &doc->groups[i];
        char *line = publish_group_line(g);
        uint32_t rank;
        char rw[16] = "-";
        if (mfd_group_rank(g, r->me, &rank)) snprintf(rw, sizeof rw, "%u", rank);
        mt_printf(&s, "%s local %s rank %s", line, gl_name(atomic_load(&r->hello_state)), rw);
        free(line);
        for (size_t j = 0; j < g->members.n; j++) {
            uint32_t k = g->members.v[j];
            if (k == r->me) continue;
            mt_printf(&s, " member%u %s@%u", k, gl_name(r->st[k].group_state), r->st[k].group_epoch);
        }
        mt_printf(&s, " why %s\n", r->hello_why && *r->hello_why ? r->hello_why : "-");
    }
    return s.len ? mt_take(&s) : mt_strdup("");
}

void publish_all(mpr_relay *r, uint64_t now) {
    r->last_publish_ms = mpr_mono_ms();
    const mpr_host *h = &r->h;
    uint32_t alive = mfc_alive(r->core, now);
    uint64_t suspect_after = r->tun.dead_ms / 3;
    uint32_t suspect = SUSPECT_NONE;
    uint64_t hs = h->uptime_ms(h->ctx);
    for (uint32_t k = 0; k < r->n; k++) {
        if (k == r->me) continue;
        bool linked;
        uint64_t rx = relay_last_rx(r, k, &linked);
        uint32_t st;
        if (!linked) st = MESH_MS_ABSENT;
        else if (!(alive >> k & 1)) st = MESH_MS_LINKED;
        else if ((hs > rx ? hs - rx : 0) > suspect_after) suspect |= 1u << k, st = MESH_MS_SUSPECT;
        else st = MESH_MS_UP;
        publish_member(r, k, st, NULL);
    }
    const mfd_doc *doc = mfc_committed(r->core);
    mpr_recon rs = {0};
    h->recon(h->ctx, &rs);
    const mfc_config *cfg = mfc_cfg(r->core);
    uint32_t leader;
    h->host_u32(h->ctx, MESH_S_LEADER, mfc_leader(r->core, &leader) ? leader : MESH_PEER_NONE);
    h->host_u32(h->ctx, MESH_S_ROLE, mfc_role_code(mfc_role_of(r->core)));
    h->host_u64(h->ctx, MESH_S_TERM, mfc_term(r->core));
    h->host_u64(h->ctx, MESH_S_STATE_VER, doc->version);
    h->host_u64(h->ctx, MESH_S_APPLIED_VER, rs.applied);
    h->host_u32(h->ctx, MESH_S_UP_MASK, alive);
    h->host_u32(h->ctx, MESH_S_SUSPECT_MASK, suspect);
    h->host_u32(h->ctx, MESH_S_QUORUM, mfc_quorum(cfg));
    h->host_u32(h->ctx, MESH_S_FLEET_FLAGS, (mfc_has_quorum(r->core, now) ? MESH_FF_QUORUM : 0) | (mfc_fixed(cfg) ? MESH_FF_FIXED : 0));
    h->host_u32(h->ctx, MESH_S_DEAD_MS, (uint32_t)r->tun.dead_ms);
    h->host_u32(h->ctx, MESH_S_COMMIT_TERM, (uint32_t)doc->term);
    uint32_t hello_state = atomic_load(&r->hello_state);
    for (uint32_t k = 0; k < r->n; k++) {
        size_t f = mesh_fmember(k);
        uint32_t st, gs;
        uint64_t ap, heard;
        const char *why;
        if (k == r->me) st = rs.status, gs = hello_state, ap = rs.applied, why = rs.reason, heard = hs;
        else {
            const mpk_beat_status *s = &r->st[k];
            bool linked;
            uint64_t rx = relay_last_rx(r, k, &linked);
            st = s->status, gs = s->group_state, ap = s->applied, why = s->reason, heard = linked ? rx : 0;
        }
        h->host_u32(h->ctx, f + MESH_FM_STATUS, st);
        h->host_u32(h->ctx, f + MESH_FM_GROUP, gs);
        h->host_u64(h->ctx, f + MESH_FM_APPLIED, ap);
        h->host_u64(h->ctx, f + MESH_FM_HEARD_MS, heard);
        h->host_str(h->ctx, f + MESH_FM_REASON, 40, why ? why : "");
    }
    if (doc->ngroups) {
        const mfd_group *g = &doc->groups[0];
        uint8_t order[MESH_MAX_SYSTEMS];
        uint32_t rank;
        mfd_group_order(g, order);
        h->host_str(h->ctx, MESH_SG_NAME, MESH_NAME_BYTES, g->name);
        h->host_u32(h->ctx, MESH_SG_EPOCH, g->epoch);
        h->host_u32(h->ctx, MESH_SG_CYCLE, g->cycle);
        h->host_u32(h->ctx, MESH_SG_MASK, mfd_group_mask(g));
        h->host_bytes(h->ctx, MESH_SG_ORDER, order, sizeof order);
        h->host_u32(h->ctx, MESH_SG_STATE, g->state == MFD_RUNNING ? 1 : 2);
        h->host_u32(h->ctx, MESH_SG_RANK, mfd_group_rank(g, r->me, &rank) ? rank : MESH_PEER_NONE);
        h->host_u32(h->ctx, MESH_SG_ON_LOSS, g->on_loss == MFD_LOSS_PAUSE ? 0 : 1);
        h->host_u32(h->ctx, MESH_SG_AUTO_MS, g->auto_ms);
    } else {
        h->host_str(h->ctx, MESH_SG_NAME, MESH_NAME_BYTES, "");
        h->host_u32(h->ctx, MESH_SG_STATE, 0);
        h->host_u32(h->ctx, MESH_SG_MASK, 0);
        h->host_u32(h->ctx, MESH_SG_RANK, MESH_PEER_NONE);
    }
    h->host_u32(h->ctx, MESH_SG_LOCAL, hello_state);
    char *ft = fleet_text(r, now, alive, suspect, &rs), *gt = groups_text(r);
    h->texts(h->ctx, ft, gt);
    free(ft);
    free(gt);
    if (mpr_mono_ms() - r->last_sched_text_ms >= 1000) {
        r->last_sched_text_ms = mpr_mono_ms();
        sched_publish(r, now, alive, suspect, &rs);
    }
    mpr_recon_free(&rs);
}

/* ---- the scheduler's texts and the fleet view */

/* The program table's slot s: its name (empty slot: false). */
static bool slot_name(mpr_relay *r, uint32_t s, char name[MESH_NAME_BYTES + 1]) {
    size_t e = MESH_PROG_TABLE + (size_t)s * MESH_PT_BYTES;
    if (r->h.mb_get32(r->h.ctx, e + MESH_PT_STATE) == MESH_ST_EMPTY) return false;
    r->h.mb_read(r->h.ctx, e + MESH_PT_NAME, (uint8_t *)name, MESH_NAME_BYTES);
    name[MESH_NAME_BYTES] = 0;
    return true;
}

static void opt_f(char *out, size_t cap, mof32 v, int p) {
    if (v.some) snprintf(out, cap, "%.*f", p, (double)v.v);
    else snprintf(out, cap, "-");
}

/* views.rs records: fleet, member, program, cell, explain, group, gmember, proposal, migration,
 * decision, note. */
static char *fleet_recs(mpr_relay *r, const mfd_doc *doc, const msm_member *ms, size_t nms, const msm_perfs *perf, bool leading, uint32_t alive, uint32_t suspect,
                        const mpr_recon *rs) {
    mt_buf o = {0};
    const mfc_config *cfg = mfc_cfg(r->core);
    uint32_t leader;
    bool has_leader = mfc_leader(r->core, &leader);
    ml_list busy = {0};
    for (size_t i = 0; i < nms; i++)
        if (ms[i].busy) ml_push(&busy, ms[i].id);
    rec(&o, "fleet");
    kvu(&o, "system", r->me);
    kvu(&o, "n", r->n);
    kv(&o, "role", mfc_role_word(mfc_role_of(r->core)));
    opt_u(&o, "leader", has_leader, leader);
    kvu(&o, "term", mfc_term(r->core));
    kvu(&o, "version", doc->version);
    kvu(&o, "applied", rs->applied);
    kvu(&o, "quorum", mfc_quorum(cfg));
    kv_own(&o, "up", mask_list(alive));
    kv_own(&o, "suspect", mask_list(suspect));
    kv(&o, "majority", mfc_fixed(cfg) ? "fixed" : mfc_has_quorum(r->core, relay_now(r)) ? "yes" : "no");
    kvu(&o, "paused", doc->sched_paused);
    kvu(&o, "autoreform", doc->autoreform);
    kv_own(&o, "cordon", mask_list(doc->cordon));
    kv_own(&o, "busy_members", ml_text(&busy));
    opt_u(&o, "migrations", leading, r->moves_done);
    kf(&o, "last_gap_ms", leading && r->last_gap_us > 0, (double)r->last_gap_us / 1e3, 1);
    rec_end(&o, NULL);
    ml_free(&busy);
    uint64_t hs = r->h.uptime_ms(r->h.ctx);
    uint32_t hello_state = atomic_load(&r->hello_state);
    for (size_t i = 0; i < nms; i++) {
        const msm_member *m = &ms[i];
        uint32_t k = m->id, st, gs;
        const char *state, *why;
        uint64_t applied;
        bool has_heard = true;
        uint64_t heard = 0;
        if (k == r->me) state = "up", applied = rs->applied, st = rs->status, why = rs->reason, gs = hello_state;
        else {
            const mpk_beat_status *b = &r->st[k];
            state = link_word(r, k, alive, suspect), applied = b->applied, st = b->status, why = b->reason, gs = b->group_state;
            uint64_t rx = relay_last_rx(r, k, &has_heard);
            heard = hs > rx ? hs - rx : 0;
        }
        if (!why) why = "";
        char *text;
        if (m->busy) text = mt_fmt("busy: %s", m->busy);
        else if (!*why || !strcmp(why, "ok")) text = mt_strdup("");
        else if (st == MESH_RS_SYNC) text = mt_fmt("last %s", why);
        else text = mt_strdup(why);
        rec(&o, "member");
        kvu(&o, "id", k);
        kv(&o, "state", state);
        kvu(&o, "leader", has_leader && leader == k);
        kv(&o, "host", m->host);
        kv(&o, "gpu", m->gpu);
        kv(&o, "arch", m->arch);
        kvu(&o, "cpus", m->cpus);
        kvu(&o, "big", m->cores_big);
        kvu(&o, "little", m->cores_little);
        kvu(&o, "ram_mb", m->ram_mb);
        kvu(&o, "avail_mb", m->mem_avail_mb);
        kvu(&o, "disk_mb", m->disk_free_mb);
        kf(&o, "share", m->gpu_share.some, m->gpu_share.v, 2);
        kf(&o, "cpu", m->load_known, m->cpu_util, 2);
        kf(&o, "psi_cpu", m->psi_cpu.some, m->psi_cpu.v, 3);
        kf(&o, "psi_mem", m->psi_mem.some, m->psi_mem.v, 3);
        kf(&o, "meshd_cpu", m->load_known, m->meshd_cpu, 2);
        kvu(&o, "slots_free", m->slots_free);
        kvu(&o, "heap_kb", m->heap_kb);
        kvu(&o, "cordoned", m->cordoned);
        kvu(&o, "busy", m->busy != NULL);
        kv(&o, "reconcile", rs_word(applied, doc->version, st));
        kvu(&o, "applied", applied);
        opt_u(&o, "heard_ms", has_heard, heard);
        kv(&o, "group", gl_name(gs));
        rec_end(&o, text);
        free(text);
    }
    for (size_t i = 0; i < doc->nprogs; i++) {
        const char *n = doc->pname[i];
        const mfd_prog *p = &doc->prog[i];
        rec(&o, "program");
        kv(&o, "name", n);
        kv_own(&o, "hash", mt_fmt("%016llx", (unsigned long long)p->hash));
        kvu(&o, "run", p->run);
        kvu(&o, "lockstep", p->lockstep);
        kv_own(&o, "at", ml_at_word(&p->at));
        kv_own(&o, "pin", p->pin.some ? ml_text(&p->pin.l) : mt_strdup("-"));
        opt_u(&o, "from", p->has_from, p->from);
        kv_own(&o, "replicas", msn_replicas_word(p->needs.replicas));
        kv_own(&o, "backends", msn_strs_join(p->needs.backends, p->needs.nbackends, ","));
        kvi(&o, "priority", p->needs.priority);
        kv(&o, "migrate", mesh_migrate_word(p->needs.migrate));
        kf(&o, "cpu", true, p->needs.cpu, 2);
        kvu(&o, "mem_mb", p->needs.mem_mb);
        char *text = p->why && *p->why ? mt_strdup(p->why) : msn_summary(&p->needs);
        rec_end(&o, text);
        free(text);
        const msp_explain *ex = msp_explains_get(&r->explains, n);
        for (size_t j = 0; j < nms; j++) {
            uint32_t k = ms[j].id;
            const msp_row *row = NULL;
            for (size_t x = 0; ex && x < ex->nrows; x++)
                if (ex->rows[x].k == k) row = &ex->rows[x];
            const msm_perf *pf = msm_perfs_get(perf, n, k);
            bool placed = p->at.some ? ml_has(&p->at.l, k) : (row && row->ok) || pf != NULL;
            const char *moving = p->has_from && p->from == k ? "from" : p->has_from && placed ? "to" : "-";
            rec(&o, "cell");
            kv(&o, "program", n);
            kvu(&o, "member", k);
            kvu(&o, "placed", placed);
            kvu(&o, "pinned", p->pin.some && ml_has(&p->pin.l, k));
            kv(&o, "moving", moving);
            if (placed && (p->needs.cpu > 0.0f || p->needs.mem_mb > 0)) {
                mt_buf res = {0};
                if (p->needs.cpu > 0.0f) mt_printf(&res, "cpu:%.2f", (double)p->needs.cpu);
                if (p->needs.mem_mb > 0) mt_printf(&res, "%smem_mb:%u", p->needs.cpu > 0.0f ? "," : "", p->needs.mem_mb);
                kv_own(&o, "reserved", mt_take(&res));
            } else kv(&o, "reserved", "-");
            kv(&o, "state", !pf ? "-" : pf->run ? "running" : "stopped");
            kv(&o, "exec", pf ? pf->exec : "-");
            kf(&o, "task_us", pf && pf->task_us.some, pf ? pf->task_us.v : 0, 1);
            kf(&o, "tasks_s", pf != NULL, pf ? pf->tasks_s : 0, 1);
            rec_end(&o, NULL);
            if (!row) continue;
            rec(&o, "explain");
            kv(&o, "program", n);
            kvu(&o, "member", k);
            if (row->ok) {
                bool on = (!ex->chosen.some || ml_has(&ex->chosen.l, k));
                kv(&o, "verdict", on ? "chosen" : "feasible");
                kf(&o, "score", true, row->score.total, 3);
                char *t = msc_explain(&row->score);
                rec_end(&o, t);
                free(t);
            } else {
                kv(&o, "verdict", "rejected");
                kv(&o, "score", "-");
                rec_end(&o, row->why);
            }
        }
    }
    for (size_t i = 0; i < doc->ngroups; i++) {
        const mfd_group *g = &doc->groups[i];
        rec(&o, "group");
        kv(&o, "name", g->name);
        kv(&o, "program", g->program);
        kvu(&o, "epoch", g->epoch);
        kvu(&o, "cycle", g->cycle);
        kv_own(&o, "members", ml_text(&g->members));
        kv(&o, "state", g->state == MFD_RUNNING ? "running" : "paused");
        kv(&o, "on_loss", g->on_loss == MFD_LOSS_PAUSE ? "pause" : "reform");
        kvu(&o, "auto_ms", g->auto_ms);
        rec_end(&o, NULL);
        for (size_t j = 0; j < g->members.n; j++) {
            uint32_t k = g->members.v[j], gs, ep, rank;
            if (k == r->me) gs = hello_state, ep = r->has_hello ? r->hello.epoch : 0;
            else gs = r->st[k].group_state, ep = r->st[k].group_epoch;
            bool has_rank = mfd_group_rank(g, k, &rank);
            rec(&o, "gmember");
            kv(&o, "group", g->name);
            kvu(&o, "member", k);
            opt_u(&o, "rank", has_rank, rank);
            kv(&o, "state", gl_name(gs));
            kvu(&o, "epoch", ep);
            rec_end(&o, NULL);
        }
    }
    if (r->group_prop) {
        rec(&o, "proposal");
        rec_end(&o, r->group_prop);
    }
    if (r->has_mig) {
        rec(&o, "migration");
        kv_own(&o, "programs", mt_join(&r->mig.mv.programs, ","));
        kvu(&o, "from", r->mig.mv.from);
        kvu(&o, "to", r->mig.mv.to);
        kvu(&o, "version", r->mig.version);
        kf(&o, "secs", true, (double)(mpr_mono_ms() - r->mig.t0_ms) / 1e3, 1);
        rec_end(&o, r->mig.mv.why);
    }
    for (size_t i = r->sched_log.n; i-- > 0;) {
        const char *d = r->sched_log.v[i], *sep = strstr(d, " s: ");
        rec(&o, "decision");
        if (sep) {
            char *at = mt_strndup(d, (size_t)(sep - d));
            kv_own(&o, "at_s", at);
            rec_end(&o, sep + 4);
        } else {
            kv(&o, "at_s", "-");
            rec_end(&o, d);
        }
    }
    if (leading) {
        const mt_strs *notes = mst_notes(r->tracker);
        for (size_t i = 0; i < notes->n; i++) {
            rec(&o, "note");
            rec_end(&o, notes->v[i]);
        }
    }
    return mt_take(&o);
}

/* `sched` (every member computes it from what it knows; the leader adds its rebalancer's state), the
 * placed word of every program slot, the fleet view, and the jobs dispatcher's model. */
void sched_publish(mpr_relay *r, uint64_t now, uint32_t alive, uint32_t suspect, const mpr_recon *rs) {
    const mpr_host *h = &r->h;
    mfd_doc doc = mfd_doc_clone(mfc_committed(r->core));
    bool leading = mfc_role_of(r->core) == MFC_LEADER;
    /* The explain: the leader's from its last pass; another member's computed here. */
    if (!leading || r->explains.n == 0) {
        mt_strs ch = {0};
        mfd_doc d = sched_pass(r, &doc, now, &ch);
        mfd_doc_free(&d);
        mt_strs_free(&ch);
    }
    size_t nms;
    msm_member *ms = sched_members(r, now, &nms);
    if (leading) mst_owner(r->tracker, ms, nms, now, &r->w);
    bool migrating = false;
    for (size_t i = 0; i < doc.nprogs; i++) migrating |= doc.prog[i].has_from;
    h->host_u32(h->ctx, MESH_S_SCHED_FLAGS, (doc.sched_paused ? MESH_SF_PAUSED : 0) | (doc.autoreform ? MESH_SF_AUTOREFORM : 0) | (migrating ? MESH_SF_MIGRATING : 0));
    h->host_u32(h->ctx, MESH_S_CORDON, doc.cordon);
    uint32_t busy = 0;
    ml_list busy_l = {0};
    for (size_t i = 0; i < nms; i++)
        if (ms[i].busy) busy |= 1u << ms[i].id, ml_push(&busy_l, ms[i].id);
    h->host_u32(h->ctx, MESH_S_BUSY_MASK, busy);
    if (leading) {
        h->host_u32(h->ctx, MESH_S_SCHED_MOVES, r->moves_done);
        h->host_u32(h->ctx, MESH_S_SCHED_GAP_US, r->last_gap_us);
    }
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        char name[MESH_NAME_BYTES + 1];
        uint32_t w = 0;
        const mfd_prog *p = slot_name(r, s, name) ? mfd_prog_get(&doc, name) : NULL;
        if (p) {
            uint32_t at = p->at.some ? ml_mask(&p->at.l) & MESH_PS_MASK : MESH_PS_ALL;
            w = MESH_PS_KNOWN | at | (p->pin.some ? MESH_PS_PINNED : 0) | (p->has_from ? MESH_PS_MIGRATING | (p->from + 1) << MESH_PS_FROM_SHIFT : 0);
        }
        h->host_u32(h->ctx, mesh_placed(s), w);
    }
    /* The text. */
    uint32_t leader;
    bool has_leader = mfc_leader(r->core, &leader);
    mt_buf t = {0};
    char lw[16] = "-", gap[32] = "-", moves[16] = "-";
    if (has_leader) snprintf(lw, sizeof lw, "%u", leader);
    if (leading) snprintf(moves, sizeof moves, "%u", r->moves_done);
    if (leading && r->last_gap_us > 0) snprintf(gap, sizeof gap, "%.1f", (double)r->last_gap_us / 1e3);
    char *cordon = mask_list(doc.cordon), *bl = ml_text(&busy_l);
    mt_printf(&t, "sched system %u leader %s version %llu paused %u autoreform %u cordon %s busy %s migrations %s last_gap_ms %s\n", r->me, lw,
              (unsigned long long)doc.version, (unsigned)doc.sched_paused, (unsigned)doc.autoreform, cordon, bl, moves, gap);
    free(cordon);
    free(bl);
    ml_free(&busy_l);
    char os[RF_FLOAT_MAX], op[RF_FLOAT_MAX];
    mt_printf(&t, "policy improve %.0f%% hold_ms %llu dwell_ms %llu per_min %u replace_ms %llu owner share<%s psi>%s for %llu ms; tick_ms %llu\n", r->w.improve * 100.0,
              (unsigned long long)r->w.hold_ms, (unsigned long long)r->w.dwell_ms, r->w.per_min, (unsigned long long)r->w.replace_ms, rf_f32(os, r->w.owner_share),
              rf_f32(op, r->w.owner_psi), (unsigned long long)r->w.owner_ms, (unsigned long long)r->w.tick_ms);
    for (size_t i = 0; i < nms; i++) {
        const msm_member *m = &ms[i];
        char share[32], pc[32], pm[32];
        opt_f(share, sizeof share, m->gpu_share, 2);
        opt_f(pc, sizeof pc, m->psi_cpu, 3);
        opt_f(pm, sizeof pm, m->psi_mem, 3);
        mt_buf rtt = {0};
        for (size_t j = 0; j < m->nrtt; j++)
            if (m->rtt_us[j].some) mt_printf(&rtt, "%s%zu:%.0f", rtt.len ? "," : "", j, (double)m->rtt_us[j].v);
        char *rt = rtt.len ? mt_take(&rtt) : mt_strdup("-");
        mt_printf(&t, "member %u %s host %s gpu %s arch %s cpus %u ram_mb %u avail_mb %u share %s cpu %.2f psi_cpu %s psi_mem %s meshd %.2f rtt_us %s%s", m->id,
                  m->up ? "up" : "down", *m->host ? m->host : "?", *m->gpu ? m->gpu : "?", m->arch, m->cpus, m->ram_mb, m->mem_avail_mb, share, (double)m->cpu_util, pc, pm,
                  (double)m->meshd_cpu, rt, m->cordoned ? " cordoned" : "");
        free(rt);
        if (m->busy) mt_printf(&t, " busy: %s", m->busy);
        mt_putc(&t, '\n');
    }
    for (size_t i = 0; i < doc.nprogs; i++) {
        const mfd_prog *p = &doc.prog[i];
        char *at = ml_at_word(&p->at), *pin = p->pin.some ? ml_text(&p->pin.l) : mt_strdup("-"), *sum = msn_summary(&p->needs);
        char from[16] = "-";
        if (p->has_from) snprintf(from, sizeof from, "%u", p->from);
        mt_printf(&t, "program %s at %s pin %s from %s %s why %s\n", doc.pname[i], at, pin, from, sum, p->why && *p->why ? p->why : "-");
        free(at), free(pin), free(sum);
    }
    if (r->has_mig) {
        char *pl = mt_join(&r->mig.mv.programs, ",");
        mt_printf(&t, "migration %s from %u to %u version %llu for %.1f s\n", pl, r->mig.mv.from, r->mig.mv.to, (unsigned long long)r->mig.version,
                  (double)(mpr_mono_ms() - r->mig.t0_ms) / 1e3);
        free(pl);
    }
    if (leading) {
        const mt_strs *notes = mst_notes(r->tracker);
        for (size_t i = 0; i < notes->n; i++) mt_printf(&t, "note %s\n", notes->v[i]);
    }
    if (r->group_prop) mt_printf(&t, "proposal %s\n", r->group_prop);
    for (size_t i = 0; i < r->sched_log.n; i++) mt_printf(&t, "log %s\n", r->sched_log.v[i]);
    /* The dry run: what a pass and an unhurried rebalance would do now. */
    msm_perfs perf = sched_perf_map(r, now);
    msp_input in = {&doc, ms, nms, &perf, &r->w};
    mt_strs ch = {0};
    msp_explains ex0 = {0};
    mfd_doc placed = msp_place(&in, &ch, &ex0);
    mfd_doc_free(&placed);
    msp_explains_free(&ex0);
    mt_buf dry = {0};
    for (size_t i = 0; i < ch.n; i++) mt_printf(&dry, "place %s\n", ch.v[i]);
    mt_strs_free(&ch);
    mst_tracker *eager = mst_new();
    msw_weights w0 = r->w;
    w0.hold_ms = 0, w0.dwell_ms = 0;
    msp_input in0 = {&doc, ms, nms, &perf, &w0};
    mst_move mv;
    if (mst_step(eager, &in0, now, false, &mv)) {
        char *pl = mt_join(&mv.programs, ",");
        mt_printf(&dry, "move %s from %u to %u: %s\n", pl, mv.from, mv.to, mv.why);
        free(pl);
        mst_move_free(&mv);
    }
    mst_free(eager);
    if (doc.ngroups) {
        const mfd_group *g = &doc.groups[0];
        ml_list members;
        char *why;
        if (msp_group_choice(&in, g, &members, &why)) {
            if (!ml_eq(&members, &g->members)) {
                char *l = ml_text(&members);
                mt_printf(&dry, "reform group %s over %s: %s\n", g->name, l, why);
                free(l);
            }
            ml_free(&members);
            free(why);
        }
    }
    char *dr = dry.len ? mt_take(&dry) : mt_strdup("nothing to change\n");
    char *all = mt_take(&t);
    char *recs = fleet_recs(r, &doc, ms, nms, &perf, leading, alive, suspect, rs);
    h->sched_texts(h->ctx, all, &r->explains, dr, recs);
    free(all), free(dr), free(recs);
    /* The jobs dispatcher picks lease members with the same model (jobs::select). */
    if (h->jobs_sched) h->jobs_sched(h->ctx, ms, nms, &doc, &perf, &r->w);
    msm_perfs_free(&perf);
    sched_members_free(ms, nms);
    mfd_doc_free(&doc);
}
