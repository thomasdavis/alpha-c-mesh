/* crates/mesh-top/src/app.rs, ported. */
#include "mesh/top_app.h"

#include "pathtrace/view.h"
#include "tui/utf8.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const MT_TAB_NAMES[MT_TABS] = {"Mesh", "Programs", "Doom", "Train", "Infer", "HTTP",
                                           "Net",  "Load",     "Fleet", "Jobs", "Pipes", "Render"};
const char MT_TAB_KEYS[MT_TABS] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '='};
const char *const MT_VIEWS[3] = {"fleet", "jobs", "pipes"};
const char *const MT_RENDER_PROGRAMS[2] = {"pathtrace", "pathtrace-live"};

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}
static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) abort();
    return p;
}
static char *xstrdup(const char *s) {
    char *d = xmalloc(strlen(s) + 1);
    strcpy(d, s);
    return d;
}
static char *xfmt(const char *f, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
static char *xfmt(const char *f, ...) {
    va_list ap, cp;
    va_start(ap, f);
    va_copy(cp, ap);
    int n = vsnprintf(NULL, 0, f, cp);
    va_end(cp);
    char *s = xmalloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, f, ap);
    va_end(ap);
    return s;
}
static void set_str(char **dst, char *s) {
    free(*dst);
    *dst = s;
}

/* ---- Rate */

void mt_rate_push(mt_rate *r, mt_instant now, uint64_t v) {
    if (r->n && v < r->v[r->n - 1]) r->n = 0; /* a reset or reload: a new count */
    if (r->n == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 8;
        r->t = xrealloc(r->t, r->cap * sizeof *r->t);
        r->v = xrealloc(r->v, r->cap * sizeof *r->v);
    }
    r->t[r->n] = now;
    r->v[r->n] = v;
    r->n++;
    while (r->n > 2 && mt_since(now, r->t[1]) >= MT_RATE_WINDOW) {
        memmove(r->t, r->t + 1, (r->n - 1) * sizeof *r->t);
        memmove(r->v, r->v + 1, (r->n - 1) * sizeof *r->v);
        r->n--;
    }
}

float mt_rate_per_s(const mt_rate *r) {
    if (r->n == 0) return 0.0f;
    mt_instant at = r->t[0], bt = r->t[r->n - 1];
    if (!(bt > at)) return 0.0f;
    return (float)(r->v[r->n - 1] - r->v[0]) / mt_secs_f32(mt_since(bt, at));
}

void mt_rate_free(mt_rate *r) {
    free(r->t);
    free(r->v);
    *r = (mt_rate){NULL, NULL, 0, 0};
}

/* ---- FleetImage */

static void kv_set(mt_fleet_image *f, const char *k, const char *v) {
    for (size_t i = 0; i < f->nkv; i++)
        if (strcmp(f->kv[i].key, k) == 0) {
            set_str(&f->kv[i].value, xstrdup(v));
            return;
        }
    f->kv = xrealloc(f->kv, (f->nkv + 1) * sizeof *f->kv);
    f->kv[f->nkv++] = (mt_kv){xstrdup(k), xstrdup(v)};
}

void mt_fleet_image_free(mt_fleet_image *f) {
    for (size_t i = 0; i < f->nkv; i++) {
        free(f->kv[i].key);
        free(f->kv[i].value);
    }
    free(f->kv);
    free(f->members);
    free(f->tiles);
    free(f->px);
    memset(f, 0, sizeof *f);
}

static size_t split_ws(char *s, char **w, size_t cap) {
    size_t n = 0;
    char *save = NULL;
    for (char *t = strtok_r(s, " \t\n\r\f\v", &save); t && n < cap; t = strtok_r(NULL, " \t\n\r\f\v", &save)) w[n++] = t;
    return n;
}

bool mt_fleet_image_parse(const uint8_t *b, size_t len, mt_fleet_image *out) {
    memset(out, 0, sizeof *out);
    const uint8_t *z = memchr(b, 0, len);
    if (!z) return false;
    char *text = tui_utf8_lossy(b, (size_t)(z - b), NULL);
    bool ok = true;
    for (char *line = text; line && *line && ok;) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *w[64];
        size_t n = split_ws(line, w, 64);
        if (n && (strcmp(w[0], "render") == 0 || strcmp(w[0], "digests") == 0)) {
            for (size_t i = 1; i + 1 < n; i += 2) kv_set(out, w[i], w[i + 1]); /* chunks(2); an odd last word is dropped */
        } else if (n >= 6 && strcmp(w[0], "member") == 0) {
            uint64_t k, folded, tiles;
            if (!mt_parse_u64(w[1], &k) || k > UINT32_MAX || !mt_parse_u64(w[3], &folded) || !mt_parse_u64(w[5], &tiles) ||
                tiles > UINT32_MAX) {
                ok = false;
                break;
            }
            out->members = xrealloc(out->members, (out->nmembers + 1) * sizeof *out->members);
            out->members[out->nmembers].k = (uint32_t)k;
            out->members[out->nmembers].folded = folded;
            out->members[out->nmembers].tiles = (uint32_t)tiles;
            out->nmembers++;
        }
        line = nl ? nl + 1 : NULL;
    }
    free(text);
    const uint8_t *rest = z + 1;
    size_t rl = len - (size_t)(z + 1 - b);
    if (!ok || rl < 4) {
        mt_fleet_image_free(out);
        return false;
    }
    size_t n = mt_get_u32(rest, rl, 0);
    if (rl < 4 + 8 * n) {
        mt_fleet_image_free(out);
        return false;
    }
    out->tiles = xmalloc(n * sizeof *out->tiles);
    out->ntiles = n;
    for (size_t t = 0; t < n; t++) {
        out->tiles[t][0] = mt_get_u32(rest, rl, 4 + 4 * (2 * t));
        out->tiles[t][1] = mt_get_u32(rest, rl, 4 + 4 * (2 * t + 1));
    }
    out->npx = rl - 4 - 8 * n;
    out->px = xmalloc(out->npx);
    memcpy(out->px, rest + 4 + 8 * n, out->npx);
    if (out->npx != n * 4 * PT_TILE_PIX) {
        mt_fleet_image_free(out);
        return false;
    }
    return true;
}

const char *mt_fleet_image_get(const mt_fleet_image *f, const char *k) {
    for (size_t i = 0; i < f->nkv; i++)
        if (strcmp(f->kv[i].key, k) == 0) return f->kv[i].value;
    return "";
}

uint64_t mt_fleet_image_num(const mt_fleet_image *f, const char *k) {
    uint64_t v;
    return mt_parse_u64(mt_fleet_image_get(f, k), &v) ? v : 0;
}

/* ---- train */

uint64_t mt_train_next_step(const mt_section *s) {
    return (uint64_t)mt_section_view_u32(s, MESH_TRAIN_T_STEP_HI) << 32 | mt_section_view_u32(s, MESH_TRAIN_T_NEXT_STEP);
}

size_t mt_train_losses(const mt_section *s, mt_loss out[256]) {
    uint64_t next = mt_train_next_step(s), from = next > 256 ? next - 256 : 0;
    size_t n = 0;
    for (uint64_t t = from; t < next; t++)
        out[n++] = (mt_loss){t, mt_section_view_f32(s, MESH_TRAIN_T_LOSS + 4 * (size_t)(t % 256))};
    return n;
}

/* ---- Machine */

const char *mt_machine_name(const mt_machine *m, char *out, size_t cap) {
    if (m->has_snap) {
        char h[97];
        mt_snapshot_hostname(&m->snap, h);
        if (*h) {
            snprintf(out, cap, "%s", h);
            return out;
        }
    }
    snprintf(out, cap, "%s", m->endpoint);
    return out;
}

bool mt_machine_system(const mt_machine *m, uint32_t *k) { return m->has_snap && mt_snapshot_system(&m->snap, k); }

const mt_section *mt_machine_program(const mt_machine *m, const char *name) {
    return m->has_snap ? mt_snapshot_by_name(&m->snap, name) : NULL;
}

static mt_prog_track *prog_track(const mt_machine *m, uint32_t slot) {
    for (size_t i = 0; i < m->nprogs; i++)
        if (m->progs[i].slot == slot) return &m->progs[i];
    return NULL;
}

float mt_machine_counter_rate(const mt_machine *m, uint32_t slot, size_t i) {
    mt_prog_track *p = prog_track(m, slot);
    return p ? mt_rate_per_s(&p->counters[i]) : 0.0f;
}

const mt_node_track *mt_machine_node(const mt_machine *m, uint32_t slot, uint32_t node) {
    for (size_t i = 0; i < m->nnodes; i++)
        if (m->nodes[i].slot == slot && m->nodes[i].node == node) return &m->nodes[i];
    return NULL;
}

bool mt_machine_stalled(const mt_machine *m, uint32_t slot, uint32_t node, mt_instant now) {
    const mt_node_track *t = mt_machine_node(m, slot, node);
    return t && mt_since(now, t->changed) > MT_STALL;
}

static int64_t lead(uint32_t e, uint32_t base) { return (int64_t)(int32_t)(e - base); }

size_t mt_machine_flame(const mt_machine *m, mt_instant now, uint32_t window_ns, mt_flame_rec **out) {
    *out = NULL;
    size_t nf = 0;
    const mt_trace_track **fresh = xmalloc((m->ntrace ? m->ntrace : 1) * sizeof *fresh);
    for (size_t i = 0; i < m->ntrace; i++)
        if (mt_since(now, m->trace[i].moved) <= MT_TRACE_STALE && m->trace[i].nrecs) fresh[nf++] = &m->trace[i];
    if (nf == 0) {
        free(fresh);
        return 0;
    }
    uint32_t base = fresh[0]->recs[fresh[0]->nrecs - 1].end;
    int64_t newest = INT64_MIN;
    for (size_t i = 0; i < nf; i++) {
        int64_t l = lead(fresh[i]->recs[fresh[i]->nrecs - 1].end, base);
        if (l > newest) newest = l;
    }
    size_t n = 0, cap = 0;
    mt_flame_rec *o = NULL;
    for (size_t i = 0; i < nf; i++) {
        const mt_trace_track *t = fresh[i];
        int64_t age = newest - lead(t->recs[t->nrecs - 1].end, base);
        uint32_t next_end = t->recs[t->nrecs - 1].end;
        for (size_t k = t->nrecs; k-- > 0;) {
            const mt_rec *r = &t->recs[k];
            age += (int64_t)(int32_t)(next_end - r->end);
            next_end = r->end;
            if (age > (int64_t)window_ns + 50000000) break;
            if (age >= 0 && age < (int64_t)window_ns) {
                if (n == cap) {
                    cap = cap ? cap * 2 : 64;
                    o = xrealloc(o, cap * sizeof *o);
                }
                o[n++] = (mt_flame_rec){*r, (uint32_t)age};
            }
        }
    }
    free(fresh);
    *out = o;
    return n;
}

size_t mt_machine_fresh_trace(const mt_machine *m, mt_instant now, mt_rec **out) {
    size_t n = 0;
    for (size_t i = 0; i < m->ntrace; i++)
        if (mt_since(now, m->trace[i].moved) <= MT_TRACE_STALE) n += m->trace[i].nrecs;
    mt_rec *o = xmalloc(n * sizeof *o);
    size_t k = 0;
    for (size_t i = 0; i < m->ntrace; i++)
        if (mt_since(now, m->trace[i].moved) <= MT_TRACE_STALE) {
            if (m->trace[i].nrecs) memcpy(o + k, m->trace[i].recs, m->trace[i].nrecs * sizeof *o);
            k += m->trace[i].nrecs;
        }
    *out = o;
    return n;
}

static void machine_init(mt_machine *m, const char *endpoint) {
    memset(m, 0, sizeof *m);
    m->endpoint = xstrdup(endpoint);
    m->status = xstrdup("connecting");
}

static void trace_clear(mt_machine *m) {
    for (size_t i = 0; i < m->ntrace; i++) free(m->trace[i].recs);
    free(m->trace);
    m->trace = NULL;
    m->ntrace = 0;
}

static void strv_free(char **v, size_t n) {
    for (size_t i = 0; i < n; i++) free(v[i]);
    free(v);
}

static void prog_free(mt_prog_track *p) {
    for (size_t i = 0; i < MESH_COUNTERS; i++) mt_rate_free(&p->counters[i]);
}

static void machine_free(mt_machine *m) {
    free(m->endpoint);
    free(m->status);
    if (m->has_snap) mt_snapshot_free(&m->snap);
    free(m->nodes);
    for (size_t i = 0; i < m->nprogs; i++) prog_free(&m->progs[i]);
    free(m->progs);
    free(m->tps);
    free(m->params);
    free(m->losses);
    mt_rate_free(&m->train_steps);
    mt_rate_free(&m->doom_shown);
    free(m->http_resp);
    trace_clear(m);
    free(m->trace_err);
    strv_free(m->log, m->nlog);
    free(m->log_err);
    strv_free(m->catalog, m->ncatalog);
    mt_net_free(&m->net);
    free(m->load_hist);
    free(m->render_name);
    free(m->render_px);
    free(m->render_err);
    if (m->has_fleet) mt_fleet_image_free(&m->fleet);
}

static void record_lock(mt_machine *m, const mt_snapshot *s) {
    char name[3 * MESH_NAME_BYTES + 1];
    uint64_t step;
    uint8_t view[32];
    mt_snapshot_lock_pair(s, name, &step, view);
    if (!*name) return;
    if (m->nparams && step < m->params[m->nparams - 1].step) m->nparams = 0; /* a reset: another run */
    size_t i = 0;
    while (i < m->nparams && m->params[i].step < step) i++;
    if (i < m->nparams && m->params[i].step == step) {
        memcpy(m->params[i].view, view, 32);
    } else {
        m->params = xrealloc(m->params, (m->nparams + 1) * sizeof *m->params);
        memmove(&m->params[i + 1], &m->params[i], (m->nparams - i) * sizeof *m->params);
        m->params[i].step = step;
        memcpy(m->params[i].view, view, 32);
        m->nparams++;
    }
    while (m->nparams > MT_PARAM_HISTORY) {
        memmove(m->params, m->params + 1, (m->nparams - 1) * sizeof *m->params);
        m->nparams--;
    }
}

static void loss_push(mt_machine *m, mt_loss l) {
    if (m->nlosses == m->caplosses) {
        m->caplosses = m->caplosses ? m->caplosses * 2 : 512;
        m->losses = xrealloc(m->losses, m->caplosses * sizeof *m->losses);
    }
    m->losses[m->nlosses++] = l;
}

static void record_losses(mt_machine *m, const mt_section *t) {
    mt_loss ring[256];
    size_t nr = mt_train_losses(t, ring);
    uint64_t next = mt_train_next_step(t);
    if (m->nlosses && m->losses[m->nlosses - 1].step >= next) m->nlosses = 0;
    bool has_after = m->nlosses > 0;
    uint64_t after = has_after ? m->losses[m->nlosses - 1].step : 0;
    for (size_t i = 0; i < nr; i++)
        if (!has_after || ring[i].step > after) loss_push(m, ring[i]);
    if (m->nlosses > MT_LOSS_POINTS) {
        mt_loss last = m->losses[m->nlosses - 1];
        size_t w = 0;
        for (size_t i = 0; i < m->nlosses; i += 2) m->losses[w++] = m->losses[i];
        m->nlosses = w;
        mt_loss l = m->losses[m->nlosses - 1];
        if (!(l.step == last.step && l.loss == last.loss)) loss_push(m, last);
    }
}

static void apply_snap(mt_machine *m, mt_snapshot s, mt_instant now) {
    double dt_ns = m->has_snap_at ? (double)mt_since(now, m->snap_at) : 0.0;
    size_t total_nodes = 0;
    for (size_t i = 0; i < s.nsections; i++) total_nodes += mt_section_nodes(&s.sections[i]);
    mt_node_track *nodes = xmalloc(total_nodes * sizeof *nodes);
    mt_prog_track *progs = xmalloc(s.nsections * sizeof *progs);
    size_t nn = 0;
    for (size_t si = 0; si < s.nsections; si++) {
        const mt_section *sec = &s.sections[si];
        uint32_t slot = mt_section_slot(sec), loads = mt_section_pt(sec, MESH_PT_LOADS);
        char name[3 * MESH_NAME_BYTES + 1];
        mt_section_name(sec, name);
        const mt_section *prev = NULL;
        if (m->has_snap) {
            const mt_section *p = mt_snapshot_by_slot(&m->snap, slot);
            if (p && mt_section_name_is(p, name) && mt_section_pt(p, MESH_PT_LOADS) == loads &&
                mt_section_nodes(p) == mt_section_nodes(sec))
                prev = p;
        }
        for (size_t n = 0; n < mt_section_nodes(sec); n++) {
            mt_node b = mt_section_node(sec, n);
            const mt_node_track *t = mt_machine_node(m, slot, (uint32_t)n);
            mt_node_track nt = {slot, (uint32_t)n, b.heartbeat, now, 0.0f};
            if (prev && t) {
                mt_node a = mt_section_node(prev, n);
                float util = t->util;
                if (dt_ns > 0.0) {
                    double u = (double)(b.busy_ns - a.busy_ns) / dt_ns;
                    u = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
                    util = (float)u;
                }
                nt.changed = b.heartbeat != t->heartbeat ? now : t->changed;
                nt.util = util;
            }
            nodes[nn++] = nt;
        }
        mt_prog_track *old = prog_track(m, slot);
        mt_prog_track pt;
        if (old && strcmp(old->name, name) == 0 && old->loads == loads) {
            pt = *old;
            memset(old, 0, sizeof *old); /* moved */
            old->slot = UINT32_MAX;
        } else {
            memset(&pt, 0, sizeof pt);
            pt.slot = slot;
            snprintf(pt.name, sizeof pt.name, "%s", name);
            pt.loads = loads;
        }
        pt.slot = slot;
        for (size_t i = 0; i < MESH_COUNTERS; i++) mt_rate_push(&pt.counters[i], now, mt_section_counter(sec, i));
        progs[si] = pt;
    }
    /* Nodes sorted by (slot, node), as the BTreeMap keeps them. */
    for (size_t i = 1; i < nn; i++)
        for (size_t j = i; j > 0 && (nodes[j - 1].slot > nodes[j].slot ||
                                     (nodes[j - 1].slot == nodes[j].slot && nodes[j - 1].node > nodes[j].node));
             j--) {
            mt_node_track t = nodes[j];
            nodes[j] = nodes[j - 1];
            nodes[j - 1] = t;
        }
    free(m->nodes);
    m->nodes = nodes;
    m->nnodes = nn;
    for (size_t i = 0; i < m->nprogs; i++) prog_free(&m->progs[i]);
    free(m->progs);
    m->progs = progs;
    m->nprogs = s.nsections;
    /* The trace keeps only slots still present. */
    size_t w = 0;
    for (size_t i = 0; i < m->ntrace; i++) {
        if (mt_snapshot_by_slot(&s, m->trace[i].slot)) m->trace[w++] = m->trace[i];
        else free(m->trace[i].recs);
    }
    m->ntrace = w;
    float tps = mt_snapshot_host_f32(&s, MESH_S_TASKS_PER_S);
    uint64_t tv = isfinite(tps) && tps > 0.0f ? (tps >= 1.8446744e19f ? UINT64_MAX : (uint64_t)tps) : 0;
    if (m->ntps == MT_SPARK_LEN) {
        memmove(m->tps, m->tps + 1, (MT_SPARK_LEN - 1) * sizeof *m->tps);
        m->ntps--;
    }
    if (!m->tps) m->tps = xmalloc(MT_SPARK_LEN * sizeof *m->tps);
    m->tps[m->ntps++] = tv;
    record_lock(m, &s);
    mt_net_update(&m->net, &s);
    mt_load l;
    if (mt_snapshot_load(&s, &l) && l.samples != m->load_seen) {
        m->load_seen = l.samples;
        if (!m->load_hist) m->load_hist = xmalloc(MT_SPARK_LEN * sizeof *m->load_hist);
        if (m->nload_hist == MT_SPARK_LEN) {
            memmove(m->load_hist, m->load_hist + 1, (MT_SPARK_LEN - 1) * sizeof *m->load_hist);
            m->nload_hist--;
        }
#define PCT(x) ((uint64_t)fminf(fmaxf(roundf((x) * 100.0f), 0.0f), 100.0f))
        m->load_hist[m->nload_hist][0] = l.share.some ? PCT(l.share.v) : 0;
        m->load_hist[m->nload_hist][1] = PCT(l.cpu);
#undef PCT
        m->nload_hist++;
    }
    const mt_section *t = mt_snapshot_by_name(&s, "train");
    if (t) {
        mt_rate_push(&m->train_steps, now, mt_train_next_step(t));
        record_losses(m, t);
    }
    const mt_section *d = mt_snapshot_by_name(&s, "doom");
    if (d) mt_rate_push(&m->doom_shown, now, mt_section_view_u32(d, MESH_DOOM_D_SHOW + MESH_SH_SEQ));
    if (m->has_snap) mt_snapshot_free(&m->snap);
    m->snap = s;
    m->has_snap = true;
    m->snap_at = now;
    m->has_snap_at = true;
    m->online = true;
    set_str(&m->status, xstrdup("online"));
}

static void apply_trace(mt_machine *m, const uint8_t *b, size_t len, mt_instant now) {
    mt_trace_prog *v;
    size_t n;
    char *err = NULL;
    if (!mt_parse_trace(b, len, &v, &n, &err)) {
        set_str(&m->trace_err, err);
        return;
    }
    set_str(&m->trace_err, NULL);
    for (size_t i = 0; i < n; i++) {
        mt_trace_track *t = NULL;
        size_t at = 0;
        while (at < m->ntrace && m->trace[at].slot < v[i].slot) at++;
        if (at < m->ntrace && m->trace[at].slot == v[i].slot) t = &m->trace[at];
        mt_instant moved = t && t->n == v[i].n ? t->moved : now;
        if (!t) {
            m->trace = xrealloc(m->trace, (m->ntrace + 1) * sizeof *m->trace);
            memmove(&m->trace[at + 1], &m->trace[at], (m->ntrace - at) * sizeof *m->trace);
            m->ntrace++;
            t = &m->trace[at];
        } else {
            free(t->recs);
        }
        *t = (mt_trace_track){v[i].slot, v[i].n, moved, v[i].recs, v[i].nrecs};
        v[i].recs = NULL;
    }
    mt_trace_free(v, n);
}

/* ---- Identity, Place */

bool mt_params_identity(const mt_app *app, mt_identity *out) {
    const mt_machine *with[64];
    size_t nw = 0;
    for (size_t i = 0; i < app->nmachines && nw < 64; i++)
        if (app->machines[i].nparams) with[nw++] = &app->machines[i];
    if (nw < 2) return false;
    bool found = false;
    uint64_t step = 0;
    for (size_t k = with[0]->nparams; k-- > 0 && !found;) {
        uint64_t s = with[0]->params[k].step;
        bool all = true;
        for (size_t i = 1; i < nw && all; i++) {
            bool has = false;
            for (size_t j = 0; j < with[i]->nparams; j++) has |= with[i]->params[j].step == s;
            all = has;
        }
        if (all) {
            found = true;
            step = s;
        }
    }
    if (!found) return false;
    const uint8_t *views[64];
    for (size_t i = 0; i < nw; i++)
        for (size_t j = 0; j < with[i]->nparams; j++)
            if (with[i]->params[j].step == step) views[i] = with[i]->params[j].view;
    /* The majority's view; a tie goes to the one seen first. */
    size_t best = 0, best_count = 0, best_first = SIZE_MAX;
    for (size_t i = 0; i < nw; i++) {
        size_t count = 0, first = SIZE_MAX;
        for (size_t j = 0; j < nw; j++)
            if (memcmp(views[j], views[i], 32) == 0) {
                count++;
                if (first == SIZE_MAX) first = j;
            }
        if (count > best_count || (count == best_count && first < best_first)) {
            best = i;
            best_count = count;
            best_first = first;
        }
    }
    memset(out, 0, sizeof *out);
    out->step = step;
    out->n = nw;
    for (size_t i = 0; i < nw; i++) {
        if (memcmp(views[i], views[best], 32) == 0) continue;
        uint32_t k;
        if (out->ndiffer < sizeof out->differ / sizeof out->differ[0]) {
            if (mt_machine_system(with[i], &k)) snprintf(out->differ[out->ndiffer], 64, "system %u", k);
            else snprintf(out->differ[out->ndiffer], 64, "%s", with[i]->endpoint);
            out->ndiffer++;
        }
    }
    out->identical = out->ndiffer == 0;
    return true;
}

bool mt_place_of(const mt_section *s, mt_place *out) {
    const uint8_t *x = mt_section_ext(s);
    if (mt_get_u32(x, MESH_PX_BYTES, MESH_PX_PLACE_CAP) == MESH_PLACE_CAP_NONE) return false;
    uint32_t mode = mt_get_u32(x, MESH_PX_BYTES, MESH_PX_PLACE), sys = mt_get_u32(x, MESH_PX_BYTES, MESH_PX_PLACE_SYS);
    memset(out, 0, sizeof *out);
    if (mode == MESH_PLACE_PIN) {
        out->kind = MT_PLACE_PIN;
        out->pin = sys;
    } else if (mode == MESH_PLACE_SPLIT) {
        out->kind = MT_PLACE_SPLIT;
        memcpy(out->assign, x + MESH_PX_PLACE_ASSIGN, MESH_PLACE_PARTS_MAX);
    } else {
        out->kind = MT_PLACE_LOCAL;
    }
    return true;
}

void mt_place_label(mt_place p, char out[32]) {
    if (p.kind == MT_PLACE_LOCAL) snprintf(out, 32, "local");
    else if (p.kind == MT_PLACE_PIN) snprintf(out, 32, "pin %u", p.pin);
    else snprintf(out, 32, "split");
}

uint32_t mt_place_input_to(mt_place p, uint32_t me) {
    return p.kind == MT_PLACE_LOCAL ? me : p.kind == MT_PLACE_PIN ? p.pin : p.assign[0];
}

mt_place mt_place_next(mt_place p, uint32_t n) {
    mt_place r;
    memset(&r, 0, sizeof r);
    if (p.kind == MT_PLACE_LOCAL) {
        r.kind = MT_PLACE_PIN;
        r.pin = 0;
    } else if (p.kind == MT_PLACE_PIN && p.pin + 1 < (n > 1 ? n : 1)) {
        r.kind = MT_PLACE_PIN;
        r.pin = p.pin + 1;
    } else if (p.kind == MT_PLACE_PIN) {
        r.kind = MT_PLACE_SPLIT;
        memset(r.assign, MESH_PART_NONE, sizeof r.assign);
    } else {
        r.kind = MT_PLACE_LOCAL;
    }
    return r;
}

bool mt_place_eq(mt_place a, mt_place b) {
    if (a.kind != b.kind) return false;
    if (a.kind == MT_PLACE_PIN) return a.pin == b.pin;
    if (a.kind == MT_PLACE_SPLIT) return memcmp(a.assign, b.assign, sizeof a.assign) == 0;
    return true;
}

size_t mt_part_runs(const uint8_t assign[MESH_PLACE_PARTS_MAX], uint32_t fanout, mt_part_run *out) {
    size_t n = 0;
    uint32_t lim = fanout < MESH_PLACE_PARTS_MAX ? fanout : MESH_PLACE_PARTS_MAX;
    for (uint32_t b = 0; b < lim; b++) {
        uint8_t k = assign[b];
        if (k == MESH_PART_NONE) continue;
        if (n && out[n - 1].system == k && out[n - 1].last + 1 == b) out[n - 1].last = b;
        else out[n++] = (mt_part_run){b, b, k};
    }
    return n;
}

/* ---- Actions and prompts */

void mt_action_free(mt_action *a) {
    free(a->line);
    a->line = NULL;
}

static mt_action none(void) { return (mt_action){MT_ACTION_NONE, 0, NULL}; }
static mt_action quit(void) { return (mt_action){MT_ACTION_QUIT, 0, NULL}; }
static mt_action send(size_t i, char *line) { return (mt_action){MT_ACTION_SEND, i, line}; }
static mt_action poll_(size_t i, char *line) { return (mt_action){MT_ACTION_POLL, i, line}; }

static void prompt_clear(mt_prompt *p) {
    free(p->name);
    free(p->line);
    free(p->what);
    memset(p, 0, sizeof *p);
}

/* ---- App */

void mt_app_init(mt_app *app, const char *const *endpoints, size_t n) {
    memset(app, 0, sizeof *app);
    app->machines = xmalloc(n * sizeof *app->machines);
    app->nmachines = n;
    for (size_t i = 0; i < n; i++) machine_init(&app->machines[i], endpoints[i]);
    app->tab = MT_TAB_MESH;
    strcpy(app->http_input, "/");
    app->last_reply = xstrdup("");
}

void mt_app_free(mt_app *app) {
    for (size_t i = 0; i < app->nmachines; i++) machine_free(&app->machines[i]);
    free(app->machines);
    prompt_clear(&app->prompt);
    free(app->last_reply);
    for (int v = 0; v < 3; v++)
        if (app->has_view[v]) mt_view_free(&app->views[v]);
    memset(app, 0, sizeof *app);
}

const mt_machine *mt_app_focused(const mt_app *app) { return &app->machines[app->focus]; }

size_t mt_app_view_machine(const mt_app *app, size_t view) {
    if (view == 2) return app->focus;
    for (size_t i = 0; i < app->nmachines; i++) {
        mt_fleet f;
        if (app->machines[i].has_snap && mt_snapshot_fleet(&app->machines[i].snap, &f) && f.role == MESH_ROLE_LEADER) return i;
    }
    return app->focus;
}

int mt_app_tab_view(const mt_app *app) {
    return app->tab == MT_TAB_FLEET ? 0 : app->tab == MT_TAB_JOBS ? 1 : app->tab == MT_TAB_PIPES ? 2 : -1;
}

const mt_view *mt_app_view(const mt_app *app, size_t v) { return app->has_view[v] ? &app->views[v] : NULL; }

void mt_app_fleet_sel(const mt_app *app, char **prog, bool *has_member, uint32_t *member) {
    *prog = NULL;
    *has_member = false;
    const mt_view *v = mt_app_view(app, 0);
    if (!v) return;
    size_t np = mt_view_count(v, "program");
    if (np) {
        size_t i = app->fsel[0] < np - 1 ? app->fsel[0] : np - 1;
        *prog = xstrdup(mt_vrec_s(mt_view_nth(v, "program", i), "name"));
    }
    uint32_t ids[256];
    size_t nm = 0;
    for (size_t i = 0, c = mt_view_count(v, "member"); i < c && nm < 256; i++) {
        uint64_t id;
        if (mt_vrec_u(mt_view_nth(v, "member", i), "id", &id)) ids[nm++] = (uint32_t)id;
    }
    if (nm) {
        size_t i = app->fsel[1] < nm - 1 ? app->fsel[1] : nm - 1;
        *has_member = true;
        *member = ids[i];
    }
}

static bool jsel_row(const mt_app *app, size_t *row) {
    const mt_view *v = mt_app_view(app, 1);
    if (!v) return false;
    if (app->has_jsel) {
        *row = app->jsel;
        return true;
    }
    *row = 0;
    for (size_t i = 0, c = mt_view_count(v, "job"); i < c; i++) {
        const char *st = mt_vrec_get(mt_view_nth(v, "job", i), "state");
        if (st && strcmp(st, "running") == 0) {
            *row = i;
            break;
        }
    }
    return true;
}

char *mt_app_job_sel(const mt_app *app) {
    const mt_view *v = mt_app_view(app, 1);
    if (!v) return NULL;
    size_t row, n = mt_view_count(v, "job");
    if (!jsel_row(app, &row) || n == 0) return NULL;
    if (row > n - 1) row = n - 1;
    return xstrdup(mt_vrec_s(mt_view_nth(v, "job", row), "job"));
}

uint32_t mt_app_cluster_size(const mt_app *app) {
    uint32_t reported = 0;
    for (size_t i = 0; i < app->nmachines; i++) {
        const mt_machine *m = &app->machines[i];
        if (m->has_snap && mt_snapshot_system(&m->snap, NULL)) {
            uint32_t n = mt_snapshot_systems(&m->snap);
            if (n > reported) reported = n;
        }
    }
    return reported > 0 ? reported : (uint32_t)(app->nmachines > 1 ? app->nmachines : 1);
}

bool mt_app_render_program(const mt_app *app, char name[64], uint32_t *tiles) {
    const mt_machine *m = mt_app_focused(app);
    if (!m->has_snap) return false;
    const mt_section *p = NULL;
    for (int i = 0; i < 2 && !p; i++) p = mt_snapshot_by_name(&m->snap, MT_RENDER_PROGRAMS[i]);
    if (!p) return false;
    uint32_t t = mt_section_view_u32(p, PT_V_TILES);
    if (!(mt_section_view_u32(p, PT_V_MAGIC) == PT_VIEW_MAGIC && t >= 1 && t <= PT_MAX_TILES)) return false;
    mt_section_name(p, name);
    *tiles = t;
    return true;
}

const mt_section *mt_app_selected(const mt_app *app) {
    const mt_machine *m = mt_app_focused(app);
    if (!m->has_snap || m->snap.nsections == 0) return NULL;
    size_t i = app->sel < m->snap.nsections - 1 ? app->sel : m->snap.nsections - 1;
    return &m->snap.sections[i];
}

size_t mt_app_tick(mt_app *app, mt_instant now, mt_action out[2]) {
    if (app->tab == MT_TAB_RENDER) {
        if (app->has_render_at && mt_since(now, app->render_at) < MT_RENDER_POLL) return 0;
        app->render_at = now;
        app->has_render_at = true;
        size_t n = 0;
        out[n++] = poll_(app->focus, xstrdup("render latest raw"));
        char name[64];
        uint32_t tiles;
        if (!mt_app_focused(app)->has_fleet && mt_app_render_program(app, name, &tiles))
            out[n++] = poll_(app->focus, xfmt("peek %s %u %u", name, (unsigned)PT_V_RGB, tiles * 4 * PT_TILE_PIX));
        return n;
    }
    int v = mt_app_tab_view(app);
    if (v >= 0) {
        if (app->has_view_at && mt_since(now, app->view_at) < MT_VIEW_POLL) return 0;
        app->view_at = now;
        app->has_view_at = true;
        out[0] = poll_(mt_app_view_machine(app, (size_t)v), xfmt("view %s", MT_VIEWS[v]));
        return 1;
    }
    if (app->tab != MT_TAB_PROGRAMS || (app->has_aux_at && mt_since(now, app->aux_at) < MT_AUX_POLL)) return 0;
    app->aux_at = now;
    app->has_aux_at = true;
    out[0] = poll_(app->focus, xstrdup("trace"));
    out[1] = poll_(app->focus, xfmt("log %d", MT_LOG_LINES));
    return 2;
}

/* text's lines (str::lines), owned; optionally trimmed and without empty ones. */
static char **lines_of(const char *text, bool trim_skip, size_t *n) {
    char **v = NULL;
    size_t cnt = 0, len = strlen(text);
    for (size_t start = 0; start < len;) {
        const char *nl = memchr(text + start, '\n', len - start);
        size_t end = nl ? (size_t)(nl - text) : len, e = end;
        if (nl && e > start && text[e - 1] == '\r') e--;
        const char *s = text + start;
        size_t l = e - start;
        start = nl ? end + 1 : len;
        if (trim_skip) {
            while (l && (*s == ' ' || (*s >= '\t' && *s <= '\r'))) s++, l--;
            while (l && (s[l - 1] == ' ' || (s[l - 1] >= '\t' && s[l - 1] <= '\r'))) l--;
            if (!l) continue;
        }
        v = xrealloc(v, (cnt + 1) * sizeof *v);
        v[cnt] = xmalloc(l + 1);
        memcpy(v[cnt], s, l);
        v[cnt][l] = 0;
        cnt++;
    }
    *n = cnt;
    return v;
}

static void first_word(const char *line, int nth, char *out, size_t cap) {
    out[0] = 0;
    const char *p = line;
    for (int i = 0; i <= nth; i++) {
        while (*p == ' ' || (*p >= '\t' && *p <= '\r')) p++;
        const char *w = p;
        while (*p && !(*p == ' ' || (*p >= '\t' && *p <= '\r'))) p++;
        if (i == nth) {
            size_t l = (size_t)(p - w);
            if (l >= cap) l = cap - 1;
            memcpy(out, w, l);
            out[l] = 0;
        }
        if (!*p) {
            if (i < nth) out[0] = 0;
            return;
        }
    }
}

static void apply_reply(mt_app *app, mt_update *u, mt_instant now) {
    char name[256];
    mt_machine_name(&app->machines[u->machine], name, sizeof name);
    mt_machine *m = &app->machines[u->machine];
    char verb[64], arg1[64];
    first_word(u->text, 0, verb, sizeof verb);
    first_word(u->text, 1, arg1, sizeof arg1);
    char *lossy = u->bytes ? tui_utf8_lossy(u->data, u->len, NULL) : NULL;
    const char *text = u->bytes ? lossy : (const char *)u->data;
    if (strcmp(verb, "trace") == 0) {
        if (u->bytes) apply_trace(m, u->data, u->len, now);
        else set_str(&m->trace_err, xstrdup(text));
    } else if (strcmp(verb, "log") == 0) {
        if (u->bytes) {
            strv_free(m->log, m->nlog);
            m->log = lines_of(text, false, &m->nlog);
            set_str(&m->log_err, NULL);
        } else {
            set_str(&m->log_err, xstrdup(text));
        }
    } else if (strcmp(verb, "view") == 0) {
        for (int v = 0; v < 3; v++)
            if (strcmp(arg1, MT_VIEWS[v]) == 0) {
                if (app->has_view[v]) mt_view_free(&app->views[v]);
                app->views[v] = mt_view_new(u->machine, now, text);
                app->has_view[v] = true;
            }
    } else if (strcmp(verb, "render") == 0) {
        mt_fleet_image f;
        if (u->bytes && mt_fleet_image_parse(u->data, u->len, &f)) {
            uint64_t folded = mt_fleet_image_num(&f, "folded");
            if (m->has_fleet_prev && now > m->fleet_prev_t) {
                double dt = mt_secs_f64(mt_since(now, m->fleet_prev_t));
                double spp = (double)mt_fleet_image_num(&f, "spp");
                f.msps = (double)(folded > m->fleet_prev_n ? folded - m->fleet_prev_n : 0) * (double)PT_TILE_PIX * spp / dt / 1e6;
            }
            m->has_fleet_prev = true;
            m->fleet_prev_t = now;
            m->fleet_prev_n = folded;
            if (m->has_fleet) mt_fleet_image_free(&m->fleet);
            m->fleet = f;
            m->has_fleet = true;
        } else {
            if (m->has_fleet) mt_fleet_image_free(&m->fleet);
            m->has_fleet = false;
            m->has_fleet_prev = false;
        }
    } else if (strcmp(verb, "peek") == 0 && (strcmp(arg1, MT_RENDER_PROGRAMS[0]) == 0 || strcmp(arg1, MT_RENDER_PROGRAMS[1]) == 0)) {
        if (u->bytes && u->len >= 3 && memcmp(u->data, "err", 3) == 0) {
            const char *s = text;
            size_t l = strlen(s);
            while (l && (*s == ' ' || (*s >= '\t' && *s <= '\r'))) s++, l--;
            while (l && (s[l - 1] == ' ' || (s[l - 1] >= '\t' && s[l - 1] <= '\r'))) l--;
            char *t = xmalloc(l + 1);
            memcpy(t, s, l);
            t[l] = 0;
            set_str(&m->render_err, t);
        } else if (u->bytes) {
            set_str(&m->render_name, xstrdup(arg1));
            free(m->render_px);
            m->render_px = u->data;
            m->render_len = u->len;
            u->data = NULL;
            m->has_render = true;
            set_str(&m->render_err, NULL);
        } else {
            set_str(&m->render_err, xstrdup(text));
        }
    } else if (strcmp(verb, "catalog") == 0 && u->bytes) {
        strv_free(m->catalog, m->ncatalog);
        m->catalog = lines_of(text, true, &m->ncatalog);
        set_str(&app->last_reply, xfmt("%s: catalog -> %zu programs", name, m->ncatalog));
    } else if (!u->bytes) {
        bool key = strncmp(u->text, "input doom ", 11) == 0 || strncmp(u->text, "key ", 4) == 0;
        if (!(key && strncmp(text, "ok", 2) == 0)) set_str(&app->last_reply, xfmt("%s: %s -> %s", name, u->text, text));
    } else {
        set_str(&app->last_reply, xfmt("%s: %s -> %zu bytes", name, u->text, u->len));
        if (strcmp(verb, "http") == 0) {
            free(m->http_resp);
            m->http_resp = u->data;
            m->http_resp_len = u->len;
            u->data = NULL;
            m->has_http_resp = true;
            app->http_scroll = 0;
        }
    }
    free(lossy);
}

void mt_app_apply(mt_app *app, mt_update *u, mt_instant now) {
    mt_machine *m = &app->machines[u->machine];
    switch (u->kind) {
    case MT_UPDATE_ONLINE:
        m->online = true;
        set_str(&m->status, xstrdup("online"));
        break;
    case MT_UPDATE_OFFLINE:
        m->online = false;
        set_str(&m->status, xstrdup(u->text));
        break;
    case MT_UPDATE_SNAP:
        apply_snap(m, u->snap, now);
        u->snap = (mt_snapshot){NULL, 0, NULL, 0};
        break;
    case MT_UPDATE_SNAP_ERROR:
        set_str(&m->status, xfmt("snap: %s", u->text));
        break;
    case MT_UPDATE_REPLY:
        apply_reply(app, u, now);
        break;
    }
    free(u->text);
    free(u->data);
    if (u->snap.bytes) mt_snapshot_free(&u->snap);
    memset(u, 0, sizeof *u);
}

static mt_action note(mt_app *app, char *s) {
    set_str(&app->last_reply, s);
    return none();
}

static mt_action toggle(mt_app *app, const char *name) {
    const mt_section *p = mt_machine_program(mt_app_focused(app), name);
    if (p) return send(app->focus, xfmt("%s %s", mt_section_running(p) ? "stop" : "start", name));
    char mn[256];
    mt_machine_name(mt_app_focused(app), mn, sizeof mn);
    return note(app, xfmt("%s: %s is not loaded", mn, name));
}

static bool is_char(tui_key k, uint32_t c) { return k.code == TUI_KEY_CHAR && k.ch == c; }
static bool is_ascii_graphic(uint32_t c) { return c >= 0x21 && c <= 0x7E; }

static void common_key(mt_app *app, tui_key k) {
    size_t n = app->nmachines;
    if (k.code != TUI_KEY_CHAR) return;
    for (int i = 0; i < MT_TABS; i++)
        if (k.ch == (uint32_t)MT_TAB_KEYS[i]) {
            app->tab = i;
            app->has_view_at = false;
            return;
        }
    if (k.ch == '[') app->focus = (app->focus + n - 1) % n;
    else if (k.ch == ']') app->focus = (app->focus + 1) % n;
}

static mt_action default_key(mt_app *app, tui_key k) {
    if (is_char(k, 'q') || k.code == TUI_KEY_ESC) return quit();
    if (is_char(k, 'd')) return toggle(app, "doom");
    if (is_char(k, 't')) return toggle(app, "train");
    if (is_char(k, 'i')) return toggle(app, "infer");
    if (is_char(k, 'X')) return send(app->focus, xstrdup("reset train"));
    common_key(app, k);
    return none();
}

static mt_action confirm(mt_app *app, size_t machine, char *line, char *what) {
    prompt_clear(&app->prompt);
    app->prompt.kind = MT_PROMPT_CONFIRM;
    app->prompt.machine = machine;
    app->prompt.line = line;
    app->prompt.what = what;
    return none();
}

static mt_action fleet_key(mt_app *app, tui_key k) {
    const mt_view *v = mt_app_view(app, 0);
    size_t np = v ? mt_view_count(v, "program") : 0, nm = v ? mt_view_count(v, "member") : 0;
    size_t at = v ? v->machine : mt_app_view_machine(app, 0);
    char *prog;
    bool has_m;
    uint32_t member = 0;
    mt_app_fleet_sel(app, &prog, &has_m, &member);
    mt_action r = none();
    size_t npm = np ? np - 1 : 0, nmm = nm ? nm - 1 : 0;
    if (k.code == TUI_KEY_UP) {
        size_t c = app->fsel[0] < npm ? app->fsel[0] : npm;
        app->fsel[0] = c ? c - 1 : 0;
    } else if (k.code == TUI_KEY_DOWN) {
        app->fsel[0] = app->fsel[0] + 1 < npm ? app->fsel[0] + 1 : npm;
    } else if (k.code == TUI_KEY_LEFT) {
        size_t c = app->fsel[1] < nmm ? app->fsel[1] : nmm;
        app->fsel[1] = c ? c - 1 : 0;
    } else if (k.code == TUI_KEY_RIGHT) {
        app->fsel[1] = app->fsel[1] + 1 < nmm ? app->fsel[1] + 1 : nmm;
    } else if (is_char(k, 'c')) {
        if (!has_m) r = note(app, xstrdup("no member selected"));
        else {
            bool cordoned = false;
            for (size_t i = 0; v && i < nm; i++) {
                const mt_vrec *mr = mt_view_nth(v, "member", i);
                uint64_t id;
                if (mt_vrec_u(mr, "id", &id) && id == member) {
                    cordoned = mt_vrec_flag(mr, "cordoned");
                    break;
                }
            }
            r = cordoned ? confirm(app, at, xfmt("sched uncordon %u", member),
                                   xfmt("uncordon system %u (new placements and job leases may go there again)", member))
                         : confirm(app, at, xfmt("sched cordon %u", member),
                                   xfmt("cordon system %u (no new placements or job leases there; what runs stays)", member));
        }
    } else if (is_char(k, 'p')) {
        if (!prog || !has_m) r = note(app, xstrdup("select a program and a member"));
        else r = confirm(app, at, xfmt("sched pin %s %u", prog, member), xfmt("pin %s to system %u (it migrates there)", prog, member));
    } else if (is_char(k, 'u')) {
        if (!prog) r = note(app, xstrdup("no program selected"));
        else r = confirm(app, at, xfmt("sched unpin %s", prog), xfmt("unpin %s (the scheduler places it again)", prog));
    } else if (is_char(k, 'z')) {
        const mt_vrec *f = v ? mt_view_first(v, "fleet") : NULL;
        bool paused = f && mt_vrec_flag(f, "paused");
        r = paused ? confirm(app, at, xstrdup("sched resume"), xstrdup("resume the scheduler (placement and rebalancing)"))
                   : confirm(app, at, xstrdup("sched pause"), xstrdup("pause the scheduler (no automatic placement changes or moves)"));
    } else if (is_char(k, 'g')) {
        const mt_vrec *g = v ? mt_view_first(v, "group") : NULL;
        if (!g) r = note(app, xstrdup("no lockstep group"));
        else {
            const char *gn = mt_vrec_s(g, "name");
            r = confirm(app, at, xfmt("group %s reform", gn),
                        xfmt("re-form group %s over every eligible up member (a new epoch: the program resets)", gn));
        }
    } else if (k.code == TUI_KEY_ESC) {
        app->tab = MT_TAB_MESH;
    } else {
        r = default_key(app, k);
    }
    free(prog);
    return r;
}

static mt_action jobs_key(mt_app *app, tui_key k) {
    const mt_view *v = mt_app_view(app, 1);
    size_t nj = v ? mt_view_count(v, "job") : 0, njm = nj ? nj - 1 : 0;
    size_t cur = 0;
    if (!jsel_row(app, &cur)) cur = 0;
    if (k.code == TUI_KEY_UP) {
        size_t c = cur < njm ? cur : njm;
        app->has_jsel = true;
        app->jsel = c ? c - 1 : 0;
    } else if (k.code == TUI_KEY_DOWN) {
        app->has_jsel = true;
        app->jsel = cur + 1 < njm ? cur + 1 : njm;
    } else if (is_char(k, 'x')) {
        char *id = mt_app_job_sel(app);
        if (!id) return note(app, xstrdup("no job selected"));
        size_t at = v ? v->machine : mt_app_view_machine(app, 1);
        mt_action r = confirm(app, at, xfmt("job cancel %s", id), xfmt("cancel job %s (what is left is given up; done items stay)", id));
        free(id);
        return r;
    } else if (k.code == TUI_KEY_ESC) {
        app->tab = MT_TAB_MESH;
    } else {
        return default_key(app, k);
    }
    return none();
}

static mt_action render_key(mt_app *app, tui_key k) {
    char name[64];
    uint32_t tiles;
    bool prog = mt_app_render_program(app, name, &tiles);
    const mt_machine *m = mt_app_focused(app);
    if (m->has_fleet) {
        if (is_char(k, 't')) {
            const char *tone = mt_fleet_image_get(&m->fleet, "tone");
            return send(app->focus, xfmt("job tone %s %s", mt_fleet_image_get(&m->fleet, "job"), strcmp(tone, "reinhard") == 0 ? "aces" : "reinhard"));
        }
        if (is_char(k, 'r')) return note(app, xstrdup("a fleet job's image is its committed passes: nothing to restart"));
    }
    if (is_char(k, 'm')) {
        app->render_marks = !app->render_marks;
        return none();
    }
    if (is_char(k, 't') && prog) {
        const mt_section *p = mt_machine_program(m, name);
        uint32_t cur = p ? mt_section_view_u32(p, PT_V_TONEMAP) : 0;
        return send(app->focus, xfmt("task %s %u %u", name, (unsigned)PT_OP_TONEMAP, (cur + 1) % PT_TONEMAPS));
    }
    if (is_char(k, 'r') && prog) return send(app->focus, xfmt("task %s %u 0", name, (unsigned)PT_OP_RESTART));
    if ((is_char(k, 't') || is_char(k, 'r')) && !prog) return note(app, xstrdup("pathtrace is not loaded on this machine"));
    if (k.code == TUI_KEY_ESC) {
        app->tab = MT_TAB_MESH;
        return none();
    }
    if (is_char(k, 'q')) return quit();
    common_key(app, k);
    return none();
}

static mt_action programs_key(mt_app *app, tui_key k) {
    const mt_machine *fm = mt_app_focused(app);
    size_t count = fm->has_snap ? fm->snap.nsections : 0, cm = count ? count - 1 : 0;
    const mt_section *s = mt_app_selected(app);
    char sname[64] = "";
    bool running = false;
    if (s) {
        mt_section_name(s, sname);
        running = mt_section_running(s);
    }
#define BY_SEL(verb) (s ? send(app->focus, xfmt("%s %s", verb, sname)) : note(app, xstrdup("no program selected")))
    if (k.code == TUI_KEY_UP) {
        size_t c = app->sel < cm ? app->sel : cm;
        app->sel = c ? c - 1 : 0;
        return none();
    }
    if (k.code == TUI_KEY_DOWN) {
        app->sel = app->sel + 1 < cm ? app->sel + 1 : cm;
        return none();
    }
    if (is_char(k, 's')) return running ? BY_SEL("stop") : BY_SEL("start");
    if (is_char(k, 'u')) return BY_SEL("upgrade");
    if (is_char(k, 'b')) return BY_SEL("rollback");
    if (is_char(k, 'r')) return BY_SEL("reset");
    if (is_char(k, 'x')) {
        if (!s) return note(app, xstrdup("no program selected"));
        prompt_clear(&app->prompt);
        app->prompt.kind = MT_PROMPT_UNLOAD;
        app->prompt.machine = app->focus;
        app->prompt.name = xstrdup(sname);
        return none();
    }
    if (is_char(k, 'L') || is_char(k, 'l')) {
        prompt_clear(&app->prompt);
        app->prompt.kind = MT_PROMPT_LOAD;
        return send(app->focus, xstrdup("catalog"));
    }
    if (k.code == TUI_KEY_ESC) {
        app->tab = MT_TAB_MESH;
        return none();
    }
    return default_key(app, k);
#undef BY_SEL
}

static mt_action prompt_key(mt_app *app, tui_key k) {
    mt_prompt p = app->prompt;
    memset(&app->prompt, 0, sizeof app->prompt); /* taken */
    mt_action r = none();
    switch (p.kind) {
    case MT_PROMPT_UNLOAD:
        if (is_char(k, 'y') || is_char(k, 'Y')) r = send(p.machine, xfmt("unload %s", p.name));
        else r = note(app, xfmt("unload %s cancelled", p.name));
        break;
    case MT_PROMPT_CONFIRM:
        if (is_char(k, 'y') || is_char(k, 'Y')) {
            app->has_view_at = false;
            r = send(p.machine, xstrdup(p.line));
        } else {
            r = note(app, xfmt("%s: cancelled", p.line));
        }
        break;
    case MT_PROMPT_LOAD: {
        const mt_machine *m = mt_app_focused(app);
        size_t ncat = m->ncatalog;
        if (k.code == TUI_KEY_ESC) {
            r = note(app, xstrdup("load cancelled"));
            break;
        }
        if (k.code == TUI_KEY_ENTER) {
            if (*p.input) r = send(app->focus, xfmt("load %s", p.input));
            else if (p.has_pick && p.pick < ncat) r = send(app->focus, xfmt("load %s", m->catalog[p.pick]));
            else r = note(app, xstrdup("load: type a program dir or pick one"));
            break;
        }
        if (k.code == TUI_KEY_UP && ncat) {
            p.pick = p.has_pick ? (p.pick ? p.pick - 1 : 0) : ncat - 1;
            p.has_pick = true;
            p.input[0] = 0;
        } else if (k.code == TUI_KEY_DOWN && ncat) {
            p.pick = p.has_pick ? (p.pick + 1 < ncat - 1 ? p.pick + 1 : ncat - 1) : 0;
            p.has_pick = true;
            p.input[0] = 0;
        } else if (k.code == TUI_KEY_BACKSPACE) {
            size_t l = strlen(p.input);
            if (l) p.input[l - 1] = 0;
        } else if (k.code == TUI_KEY_CHAR && is_ascii_graphic(k.ch) && strlen(p.input) < 128) {
            size_t l = strlen(p.input);
            p.input[l] = (char)k.ch;
            p.input[l + 1] = 0;
            p.has_pick = false;
        }
        app->prompt = p;
        return none();
    }
    case MT_PROMPT_NONE:
        return none();
    }
    prompt_clear(&p);
    return r;
}

static mt_action http_key(mt_app *app, tui_key k) {
    size_t l = strlen(app->http_input);
    if (k.code == TUI_KEY_ESC) app->tab = MT_TAB_MESH;
    else if (k.code == TUI_KEY_ENTER) return send(app->focus, xfmt("http %s", l ? app->http_input : "/"));
    else if (k.code == TUI_KEY_BACKSPACE) {
        if (l) app->http_input[l - 1] = 0;
    } else if (k.code == TUI_KEY_UP) app->http_scroll = app->http_scroll ? (uint16_t)(app->http_scroll - 1) : 0;
    else if (k.code == TUI_KEY_DOWN) app->http_scroll = app->http_scroll == UINT16_MAX ? UINT16_MAX : (uint16_t)(app->http_scroll + 1);
    else if (k.code == TUI_KEY_PAGE_UP) app->http_scroll = app->http_scroll > 10 ? (uint16_t)(app->http_scroll - 10) : 0;
    else if (k.code == TUI_KEY_PAGE_DOWN) app->http_scroll = app->http_scroll > UINT16_MAX - 10 ? UINT16_MAX : (uint16_t)(app->http_scroll + 10);
    else if (k.code == TUI_KEY_CHAR && is_ascii_graphic(k.ch) && l < MT_HTTP_INPUT_MAX) {
        app->http_input[l] = (char)k.ch;
        app->http_input[l + 1] = 0;
    }
    return none();
}

static uint32_t ascii_lower(uint32_t c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

mt_action mt_app_handle_key(mt_app *app, tui_key k) {
    if (k.kind == TUI_KEY_RELEASE) return none();
    if ((k.mods & TUI_MOD_CONTROL) && is_char(k, 'c')) return quit();
    if (app->prompt.kind != MT_PROMPT_NONE) return prompt_key(app, k);
    size_t n = app->nmachines;
    if (k.code == TUI_KEY_TAB) {
        app->tab = (app->tab + 1) % MT_TABS;
        app->has_view_at = false;
        return none();
    }
    if (k.code == TUI_KEY_BACKTAB) {
        app->tab = (app->tab + MT_TABS - 1) % MT_TABS;
        app->has_view_at = false;
        return none();
    }
    if (k.code == TUI_KEY_F && k.f == 1) {
        app->focus = (app->focus + n - 1) % n;
        return none();
    }
    if (k.code == TUI_KEY_F && k.f == 2) {
        app->focus = (app->focus + 1) % n;
        return none();
    }
    switch (app->tab) {
    case MT_TAB_HTTP: return http_key(app, k);
    case MT_TAB_DOOM: {
        char key = 0;
        if (k.code == TUI_KEY_UP) key = 'w';
        else if (k.code == TUI_KEY_DOWN) key = 's';
        else if (k.code == TUI_KEY_LEFT) key = 'a';
        else if (k.code == TUI_KEY_RIGHT) key = 'd';
        else if (k.code == TUI_KEY_CHAR && k.ch < 0x80 && strchr("wsadqe", (int)ascii_lower(k.ch)) && ascii_lower(k.ch))
            key = (char)ascii_lower(k.ch);
        if (key) return send(app->focus, xfmt("input doom %c", key));
        if (is_char(k, 'p') || is_char(k, 'v')) {
            const mt_machine *m = mt_app_focused(app);
            char mn[256];
            mt_machine_name(m, mn, sizeof mn);
            const mt_section *d = mt_machine_program(m, "doom");
            mt_place cur;
            if (!d) return note(app, xfmt("%s: doom is not loaded", mn));
            if (!mt_place_of(d, &cur)) return note(app, xfmt("%s: doom declares no placement", mn));
            mt_place to;
            memset(&to, 0, sizeof to);
            if (is_char(k, 'v')) {
                if (cur.kind == MT_PLACE_SPLIT) to.kind = MT_PLACE_LOCAL;
                else {
                    to.kind = MT_PLACE_SPLIT;
                    memset(to.assign, MESH_PART_NONE, sizeof to.assign);
                }
            } else {
                to = mt_place_next(cur, mt_app_cluster_size(app));
            }
            char label[32];
            mt_place_label(to, label);
            return send(app->focus, xfmt("place doom %s", label));
        }
        if (k.code == TUI_KEY_ESC) app->tab = MT_TAB_MESH;
        else common_key(app, k);
        return none();
    }
    case MT_TAB_PROGRAMS: return programs_key(app, k);
    case MT_TAB_FLEET: return fleet_key(app, k);
    case MT_TAB_JOBS: return jobs_key(app, k);
    case MT_TAB_RENDER: return render_key(app, k);
    default: return default_key(app, k);
    }
}

/* ---- update constructors */

mt_update mt_update_snap(size_t i, mt_snapshot s) {
    mt_update u;
    memset(&u, 0, sizeof u);
    u.kind = MT_UPDATE_SNAP;
    u.machine = i;
    u.snap = s;
    return u;
}

mt_update mt_update_reply_bytes(size_t i, const char *line, const void *data, size_t len) {
    mt_update u;
    memset(&u, 0, sizeof u);
    u.kind = MT_UPDATE_REPLY;
    u.machine = i;
    u.text = xstrdup(line);
    u.bytes = true;
    u.data = xmalloc(len);
    if (len) memcpy(u.data, data, len);
    u.len = len;
    return u;
}

mt_update mt_update_reply_text(size_t i, const char *line, const char *text) {
    mt_update u;
    memset(&u, 0, sizeof u);
    u.kind = MT_UPDATE_REPLY;
    u.machine = i;
    u.text = xstrdup(line);
    u.data = (uint8_t *)xstrdup(text);
    u.len = strlen(text);
    return u;
}

mt_update mt_update_offline(size_t i, const char *why) {
    mt_update u;
    memset(&u, 0, sizeof u);
    u.kind = MT_UPDATE_OFFLINE;
    u.machine = i;
    u.text = xstrdup(why);
    return u;
}

mt_update mt_update_online(size_t i) {
    mt_update u;
    memset(&u, 0, sizeof u);
    u.kind = MT_UPDATE_ONLINE;
    u.machine = i;
    return u;
}
