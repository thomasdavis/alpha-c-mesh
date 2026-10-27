/* This member's F_PERF (load.rs perf_report and its frame): its CPUs and RAM, each loaded program's
 * measured performance (the pload records just sampled), its reconcile failures, the link RTTs
 * measured from the others' echoes, and the echoes of theirs; put on the perf board and broadcast. */
#include "fleetd_int.h"

#include "mesh/abi.h"
#include "mesh/buf.h"
#include "mesh/prog_table.h"
#include "mesh/samplers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static mesh_fleetd *PERF; /* the hook has no context: one fleet member per process */

static uint32_t u32_at(const uint8_t *b, size_t o) {
    uint32_t v;
    memcpy(&v, b + o, 4);
    return v;
}
static float f32_at(const uint8_t *b, size_t o) {
    float v;
    memcpy(&v, b + o, 4);
    return v;
}

static void perf_report(mesh_shared *sh, const mesh_host_sample *h, const uint8_t *cls, size_t ncls, const uint8_t (*recs)[MESH_PLOAD_BYTES],
                        size_t n) {
    mesh_fleetd *f = PERF;
    mpr_relay *r = f ? atomic_load(&f->relay) : NULL;
    if (!r) return;
    mesh_mailbox mb = sh->mb;
    msr_report rep = {.t_us = (uint64_t)mesh_mono_us(), .ram_mb = h->mem_total_mb, .disk_free_mb = h->disk_free_mb};
    for (size_t i = 0; i < h->ncpus && i < MESH_MAX_CPUS; i++) rep.cpus += h->have[i];
    for (size_t i = 0; i < ncls; i++) {
        rep.big += cls[i] == MESH_CORE_BIG;
        rep.little += cls[i] == MESH_CORE_LITTLE;
    }
    rep.heap_kb = mesh_host_block_get_u32(&sh->host, MESH_S_HEAP_RUN) * (uint32_t)(MESH_VIEW_PAGE / 1024);
    uint32_t progs = mesh_host_block_get_u32(&sh->host, MESH_S_PROGRAMS);
    rep.slots_free = progs < MESH_PROG_SLOTS ? MESH_PROG_SLOTS - progs : 0;
    pthread_mutex_lock(&f->mu);
    for (size_t i = 0; i < f->nfails; i++) msr_add_fail(&rep, f->fails[i].name, f->fails[i].hash, f->fails[i].unsupported, f->fails[i].why);
    pthread_mutex_unlock(&f->mu);
    for (uint32_t s = 0; s < n; s++) {
        const uint8_t *rec = recs[s];
        uint32_t fl = u32_at(rec, MESH_PL_FLAGS);
        uint32_t st = mesh_mailbox_get(mb, mesh_prog_entry(s) + MESH_PT_STATE);
        if (!(fl & MESH_PLF_LOADED) || st == MESH_ST_EMPTY) continue;
        msm_perf p;
        memset(&p, 0, sizeof p);
        p.hash = mesh_mailbox_get64(mb, mesh_prog_entry(s) + MESH_PT_PTX_HASH);
        uint32_t ex = mesh_mailbox_get(mb, mesh_prog_ext(s) + MESH_PX_EXEC);
        snprintf(p.exec, sizeof p.exec, "%s", ex < 4 ? MESH_BACKENDS[ex] : "?");
        p.run = st == MESH_ST_RUNNING;
        float task = 0;
        bool has_task = false;
        if (fl & MESH_PLF_TASK) task = f32_at(rec, MESH_PL_TASK_US), has_task = true;
        else if (fl & MESH_PLF_LAT) task = f32_at(rec, MESH_PL_P50_US), has_task = true;
        p.task_us = (mof32){has_task && task > 0, has_task && task > 0 ? task : 0};
        p.tasks_s = f32_at(rec, MESH_PL_TASKS_S);
        p.cpu = fl & MESH_PLF_HOST ? f32_at(rec, MESH_PL_CPU10) : 0;
        p.share = (mof32){(fl & MESH_PLF_GPU) != 0, fl & MESH_PLF_GPU ? f32_at(rec, MESH_PL_SHARE10) : 0};
        if (fl & MESH_PLF_HOST) {
            uint64_t rss;
            memcpy(&rss, rec + MESH_PL_RSS, 8);
            p.rss_mb = (float)rss / 1048576.0f;
        }
        char name[MESH_NAME_BYTES + 1];
        mesh_pt_name_of(mb, s, name);
        msr_add_prog(&rep, name, &p);
    }
    uint32_t k[MESH_MAX_SYSTEMS];
    uint64_t t[MESH_MAX_SYSTEMS], held[MESH_MAX_SYSTEMS];
    size_t ne = mpr_board_echoes(r, k, t, held);
    for (size_t i = 0; i < ne; i++) msr_add_echo(&rep, k[i], t[i], held[i]);
    float us[MESH_MAX_SYSTEMS];
    size_t nr = mpr_board_rtts(r, k, us);
    for (size_t i = 0; i < nr; i++) msr_add_rtt(&rep, k[i], us[i]);
    char *text = msr_encode(&rep);
    size_t len = strlen(text);
    if (len > MESH_F_PERF_MAX) len = MESH_F_PERF_MAX;
    uint8_t *fr = mesh_xmalloc(5 + len);
    fr[0] = MESH_F_PERF;
    uint32_t l = (uint32_t)len;
    memcpy(fr + 1, &l, 4);
    memcpy(fr + 5, text, len);
    mesh_shared_broadcast(sh, fr, 5 + len);
    free(fr);
    free(text);
    mpr_board_own(r, &rep);
    msr_free(&rep);
}

void fd_perf_set(mesh_fleetd *f) {
    PERF = f;
    mesh_load_perf = f ? perf_report : NULL;
}
