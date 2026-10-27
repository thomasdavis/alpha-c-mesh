/* load.rs Prog::sample, in the Rust's float widths. */
#include "mesh/load_prog.h"
#include "mesh/buf.h"
#include "mesh/cpuuse.h"

#include <stdlib.h>
#include <string.h>

mesh_nl mesh_nl_read(mesh_mailbox mb, uint32_t s, uint32_t n, uint32_t *max_gap_ns, uint32_t *win) {
    size_t r = mesh_node_load(s, n);
    mesh_nl x = {mesh_mailbox_get64(mb, r + MESH_NL_OBS_NS), mesh_mailbox_get64(mb, r + MESH_NL_LOST_NS), mesh_mailbox_get(mb, r + MESH_NL_GAPS),
                 mesh_mailbox_get(mb, r + MESH_NL_QUEUE_US)};
    *max_gap_ns = mesh_mailbox_get(mb, r + MESH_NL_MAX_GAP_NS);
    *win = mesh_mailbox_get(mb, r + MESH_NL_WIN);
    return x;
}

void mesh_prog_sampler_reset(mesh_prog_sampler *p) {
    for (size_t i = 0; i < p->nwin; i++) free(p->win[i].lat);
    memset(p, 0, sizeof *p);
}

static void pf(uint8_t *b, size_t o, float v) { memcpy(b + o, &v, 4); }
static void pu(uint8_t *b, size_t o, uint32_t v) { for (int i = 0; i < 4; i++) b[o + (size_t)i] = (uint8_t)(v >> (8 * i)); }
static void pu64(uint8_t *b, size_t o, uint64_t v) { for (int i = 0; i < 8; i++) b[o + (size_t)i] = (uint8_t)(v >> (8 * i)); }

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void mesh_prog_sample(mesh_prog_sampler *p, mesh_mailbox mb, uint32_t s, double dt, uint32_t win, uint8_t out[MESH_PLOAD_BYTES], mesh_nl *dsum,
                      uint32_t *max_gap_out) {
    memset(out, 0, MESH_PLOAD_BYTES);
    size_t e = mesh_prog_entry(s);
    uint64_t hash = mesh_mailbox_get64(mb, e + MESH_PT_PTX_HASH);
    uint32_t loads = mesh_mailbox_get(mb, e + MESH_PT_LOADS);
    uint32_t nodes = mesh_mailbox_get(mb, e + MESH_PT_NODES);
    if (nodes > MESH_PROG_MAX_NODES) nodes = MESH_PROG_MAX_NODES;
    bool fresh = !p->started || hash != p->hash || loads != p->loads || nodes != p->nodes;
    /* The CPU executors' cost (wasm lanes, native workers): cumulative, baselined at the image's load. */
    uint32_t exec = mesh_mailbox_get(mb, mesh_prog_ext(s) + MESH_PX_EXEC);
    bool host = exec == MESH_BACKEND_WASM || exec == MESH_BACKEND_NATIVE;
    uint64_t cpu, rss;
    uint32_t units;
    mesh_slot_use_get(&mesh_cpuuse_slots[s], &cpu, &rss, &units);
    if (fresh) {
        mesh_prog_sampler_reset(p);
        p->started = true;
        p->hash = hash;
        p->loads = loads;
        p->nodes = nodes;
        p->cpu_base = p->cpu_last = cpu;
    }
    uint64_t cpu_d = cpu > p->cpu_last ? cpu - p->cpu_last : 0;
    if (cpu > p->cpu_last) p->cpu_last = cpu;
    /* GPU share: the node load records; the longest gap of the window that just closed. */
    mesh_nl d = {0, 0, 0, 0};
    uint32_t max_gap = 0;
    uint64_t busy_ns = 0, busy_tasks = 0;
    for (uint32_t n = 0; n < nodes; n++) {
        uint32_t mg, w;
        mesh_nl now = mesh_nl_read(mb, s, n, &mg, &w);
        if (w == win && mg > max_gap) max_gap = mg;
        size_t r = mesh_node_rec(s, n);
        uint64_t bn = mesh_mailbox_get64(mb, r + MESH_N_BUSY_NS);
        uint32_t bt = mesh_mailbox_get(mb, r + MESH_N_TASKS);
        if (!fresh) {
            mesh_nl dd = mesh_nl_delta(&now, &p->nl[n]);
            mesh_nl_add(&d, &dd);
            if (bn >= p->busy_ns[n]) { busy_ns += bn - p->busy_ns[n]; busy_tasks += (uint64_t)(uint32_t)(bt - p->busy_tasks[n]); }
        }
        p->nl[n] = now;
        p->busy_ns[n] = bn;
        p->busy_tasks[n] = bt;
    }
    /* The trace records written since the last sample (at most a ring's worth). */
    uint32_t tn = mesh_mailbox_get(mb, mesh_prog_ctl(s) + MESH_P_TRACE_N);
    uint32_t *lat = NULL;
    size_t nlat = 0;
    if (!fresh) {
        uint32_t nw = tn - p->trace_n;
        if (nw > MESH_TRACE_PER_PROG) nw = MESH_TRACE_PER_PROG;
        lat = mesh_xmalloc((nw ? nw : 1) * sizeof *lat);
        for (uint32_t i = tn - nw; i != tn; i++) {
            size_t r = mesh_trace_rec(s, i);
            uint32_t w0 = mesh_mailbox_get(mb, r);
            if ((w0 & 0xffu) != s || (w0 >> 16) == MESH_OP_INIT) continue;
            lat[nlat++] = mesh_mailbox_get(mb, r + 12) - mesh_mailbox_get(mb, r + 8);
        }
    }
    p->trace_n = tn;
    uint32_t done = mesh_mailbox_get(mb, mesh_prog_ctl(s) + MESH_P_DONE);
    uint64_t tasks = fresh ? 0 : (uint64_t)(uint32_t)(done - p->done);
    p->done = done;
    if (fresh) {
        pu(out, MESH_PL_FLAGS, MESH_PLF_LOADED | (host ? MESH_PLF_HOST : 0));
        if (host) { pu64(out, MESH_PL_RSS, rss); pu(out, MESH_PL_UNITS, units); }
        free(lat);
        *dsum = d;
        *max_gap_out = 0;
        return;
    }
    if (p->nwin == MESH_LOAD_WINDOW) {
        free(p->win[0].lat);
        memmove(p->win, p->win + 1, (MESH_LOAD_WINDOW - 1) * sizeof *p->win);
        p->nwin--;
    }
    mesh_prog_window *nwn = &p->win[p->nwin++];
    nwn->d = d;
    nwn->lat = lat;
    nwn->nlat = nlat;
    nwn->busy_ns = busy_ns;
    nwn->busy_tasks = busy_tasks;
    nwn->tasks = tasks;
    nwn->cpu_d = cpu_d;
    mesh_nl w = {0, 0, 0, 0};
    uint64_t wb0 = 0, wb1 = 0, wdone = 0, wcpu = 0;
    size_t wt = 0, nall = 0;
    for (size_t i = 0; i < p->nwin; i++) nall += p->win[i].nlat;
    uint32_t *all = mesh_xmalloc((nall ? nall : 1) * sizeof *all);
    size_t k = 0;
    for (size_t i = 0; i < p->nwin; i++) {
        mesh_prog_window *x = &p->win[i];
        mesh_nl_add(&w, &x->d);
        memcpy(all + k, x->lat, x->nlat * sizeof *all);
        k += x->nlat;
        wb0 += x->busy_ns;
        wb1 += x->busy_tasks;
        wt++;
        wdone += x->tasks;
        wcpu += x->cpu_d;
    }
    qsort(all, nall, sizeof *all, cmp_u32);
    if (wb1 > 0) { p->has_task_us = true; p->task_us = (float)((double)wb0 / (double)wb1 / 1e3); }
    mesh_share one = mesh_share_of(&d), ten = mesh_share_of(&w);
    uint32_t flags = MESH_PLF_LOADED;
    if ((one.has_share || ten.has_share) && ten.has_share) {
        flags |= MESH_PLF_GPU;
        pf(out, MESH_PL_SHARE, one.has_share ? one.share : ten.share);
        pf(out, MESH_PL_SHARE10, ten.share);
        pf(out, MESH_PL_SLICE_US, ten.slice_us);
        pf(out, MESH_PL_GAP_US, ten.gap_us);
        pf(out, MESH_PL_QUEUE_US, ten.queue_us);
    }
    pf(out, MESH_PL_MAX_GAP_US, (float)max_gap / 1e3f);
    pf(out, MESH_PL_OBS, (float)((double)d.obs / 1e9 / dt));
    if (nall) {
        flags |= MESH_PLF_LAT;
        pu(out, MESH_PL_LAT_N, (uint32_t)nall);
        const size_t offs[3] = {MESH_PL_P50_US, MESH_PL_P90_US, MESH_PL_P99_US};
        const double qs[3] = {0.5, 0.9, 0.99};
        for (int i = 0; i < 3; i++) pf(out, offs[i], (float)mesh_quantile(all, nall, qs[i]) / 1e3f);
        pf(out, MESH_PL_MAX_US, (float)all[nall - 1] / 1e3f);
    }
    free(all);
    if (p->has_task_us) { flags |= MESH_PLF_TASK; pf(out, MESH_PL_TASK_US, p->task_us); }
    pf(out, MESH_PL_TASKS_S, (float)((double)wdone / ((double)wt * dt)));
    if (nodes > 0) {
        double b = ((double)busy_ns / 1e9) / ((double)nodes * dt);
        pf(out, MESH_PL_BUSY, (float)(b < 1.0 ? b : 1.0));
    }
    if (host) {
        flags |= MESH_PLF_HOST;
        pu64(out, MESH_PL_CPU_NS, cpu > p->cpu_base ? cpu - p->cpu_base : 0);
        pu64(out, MESH_PL_RSS, rss);
        pf(out, MESH_PL_CPU, (float)((double)cpu_d / 1e9 / dt));
        pf(out, MESH_PL_CPU10, (float)((double)wcpu / 1e9 / ((double)wt * dt)));
        if (wdone > 0) pf(out, MESH_PL_CPU_TASK_US, (float)((double)wcpu / 1e3 / (double)wdone));
        pu(out, MESH_PL_UNITS, units);
    }
    pu(out, MESH_PL_FLAGS, flags);
    *dsum = d;
    *max_gap_out = max_gap;
}
