/* The load and NIC samplers. */
#include "mesh/samplers.h"
#include "mesh/cpuuse.h"
#include "mesh/load_prog.h"
#include "mesh/log.h"
#include "mesh/nic.h"

#include <stdlib.h>
#include <string.h>

void (*mesh_load_perf)(mesh_shared *sh, const mesh_host_sample *h, const uint8_t *cls, size_t ncls, const uint8_t (*recs)[MESH_PLOAD_BYTES], size_t n);

#define SAMPLE_NS 1000000000ll

static void sleep_until(int64_t t0) {
    int64_t left = SAMPLE_NS - (mesh_mono_ns() - t0);
    if (left > 0) mesh_sleep_us(left / 1000);
}

/* Says an error once per distinct text. */
static void once(char **said, const char *e, const char *what) {
    if (*said && strcmp(*said, e) == 0) return;
    mesh_error("%s: %s", what, e);
    free(*said);
    *said = mesh_xstrdup(e);
}

void mesh_load_sampler(mesh_shared *sh) {
    static uint8_t cls[MESH_MAX_CPUS];
    const char *src = mesh_core_classes(cls, MESH_MAX_CPUS);
    bool psi = mesh_has_psi();
    mesh_host_sample *prev = mesh_xcalloc(1, sizeof *prev), *h = mesh_xcalloc(1, sizeof *h);
    bool has_prev = false;
    int64_t prev_at = 0;
    mesh_prog_sampler *progs = mesh_xcalloc(MESH_PROG_SLOTS, sizeof *progs);
    mesh_nl gwin[MESH_LOAD_WINDOW];
    size_t ngwin = 0;
    uint32_t samples = 0;
    char *said = NULL;
    bool logged = false;
    mesh_host_derived *der = mesh_xcalloc(1, sizeof *der);
    uint8_t (*recs)[MESH_PLOAD_BYTES] = mesh_xcalloc(MESH_PROG_SLOTS, MESH_PLOAD_BYTES);
    for (;;) {
        int64_t t0 = mesh_mono_ns();
        char *err = NULL;
        if (mesh_host_read(h, sh->programs, psi, &err) == 0) {
            if (said) { mesh_info("load: host counters readable again"); free(said); said = NULL; }
            if (has_prev) {
                double dt = (double)(t0 - prev_at) / 1e9;
                mesh_mailbox mb = sh->mb;
                /* Close the load window: this sample takes the longest gaps of window `win`. */
                uint32_t win = mesh_mailbox_get(mb, MESH_H_LOAD_WIN);
                mesh_nl d = {0, 0, 0, 0};
                uint32_t max_gap = 0, execs[MESH_PROG_SLOTS];
                for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
                    execs[s] = mesh_mailbox_get(mb, mesh_prog_ext(s) + MESH_PX_EXEC);
                    if (mesh_mailbox_get(mb, mesh_prog_entry(s) + MESH_PT_STATE) == MESH_ST_EMPTY) {
                        mesh_prog_sampler_reset(&progs[s]);
                        memset(recs[s], 0, MESH_PLOAD_BYTES);
                        continue;
                    }
                    mesh_nl pd;
                    uint32_t mg;
                    mesh_prog_sample(&progs[s], mb, s, dt, win, recs[s], &pd, &mg);
                    mesh_nl_add(&d, &pd);
                    if (mg > max_gap) max_gap = mg;
                }
                mesh_mailbox_set(mb, MESH_H_LOAD_WIN, win + 1);
                if (ngwin == MESH_LOAD_WINDOW) { memmove(gwin, gwin + 1, (MESH_LOAD_WINDOW - 1) * sizeof *gwin); ngwin--; }
                gwin[ngwin++] = d;
                mesh_nl w = {0, 0, 0, 0};
                for (size_t i = 0; i < ngwin; i++) mesh_nl_add(&w, &gwin[i]);
                mesh_host_derive(h, prev, cls, MESH_MAX_CPUS, dt, der);
                samples++;
                mesh_share s1 = mesh_share_of(&d), s10 = mesh_share_of(&w);
                uint64_t up = mesh_host_block_uptime_ms(&sh->host);
                uint8_t m[MESH_MACHINE_BYTES];
                mesh_machine_bytes(m, h, der, cls, MESH_MAX_CPUS, &s1, &s10, max_gap, (float)((double)d.gaps / dt), (float)((double)d.obs / 1e9 / dt),
                                   up ? up : 1, samples, (uint32_t)((mesh_mono_ns() - t0) / 1000));
                mesh_exec_totals(m, (const uint8_t (*)[MESH_PLOAD_BYTES])recs, execs, MESH_PROG_SLOTS, atomic_load(&mesh_cpuuse_workers),
                                 atomic_load(&mesh_cpuuse_worker_spawns), atomic_load(&mesh_cpuuse_worker_crashes), mesh_log_dropped());
                uint8_t rep[MESH_LOADREP_BYTES];
                mesh_loadrep(rep, m);
                uint32_t me = sh->me < MESH_MAX_SYSTEMS ? sh->me : 0;
                uint64_t at = up ? up : 1;
                uint8_t atb[8];
                for (int i = 0; i < 8; i++) atb[i] = (uint8_t)(at >> (8 * i));
                mesh_host_field f[3 + MESH_PROG_SLOTS];
                f[0] = (mesh_host_field){MESH_S_LOAD, m, sizeof m};
                f[1] = (mesh_host_field){mesh_mload(me) + MESH_ML_SHARE, rep, sizeof rep};
                f[2] = (mesh_host_field){mesh_mload(me) + MESH_ML_AT_MS, atb, 8};
                for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) f[3 + s] = (mesh_host_field){mesh_pload(s), recs[s], MESH_PLOAD_BYTES};
                mesh_host_block_set_many(&sh->host, f, 3 + MESH_PROG_SLOTS);
                /* The load page: the same bytes for GPU programs. */
                uint8_t *hb = mesh_xmalloc(MESH_SNAP_HOST);
                mesh_host_block_bytes(&sh->host, hb);
                mesh_mailbox_write(mb, MESH_LOAD_PAGE, hb + MESH_S_LOAD, MESH_S_LOAD_END - MESH_S_LOAD, MESH_S_LOAD_END - MESH_S_LOAD);
                free(hb);
                if (sh->peer) {
                    uint8_t fr[1 + MESH_LOADREP_BYTES];
                    fr[0] = MESH_F_LOADREP;
                    memcpy(fr + 1, rep, sizeof rep);
                    mesh_shared_broadcast(sh, fr, sizeof fr);
                    if (mesh_load_perf) mesh_load_perf(sh, h, cls, MESH_MAX_CPUS, (const uint8_t (*)[MESH_PLOAD_BYTES])recs, MESH_PROG_SLOTS);
                }
                if (!logged) {
                    logged = true;
                    size_t nb = 0, nl = 0, on = 0;
                    for (size_t i = 0; i < MESH_MAX_CPUS; i++) { nb += cls[i] == MESH_CORE_BIG; nl += cls[i] == MESH_CORE_LITTLE; }
                    for (size_t i = 0; i < h->ncpus; i++) on += h->have[i];
                    mesh_info("load: sampling every 1s: %zu CPUs (%zu big, %zu little, by %s), PSI %s, sample cost %lld us", on, nb, nl, src, psi ? "yes" : "no",
                              (long long)((mesh_mono_ns() - t0) / 1000));
                }
            }
            mesh_host_sample *t = prev;
            prev = h;
            h = t;
            has_prev = true;
            prev_at = t0;
        } else {
            /* Loud once per distinct error; S_LOAD_AT_MS stops advancing, so viewers show the sample's age. */
            once(&said, err, "load: cannot read the host counters");
            free(err);
        }
        if (atomic_load(sh->quit)) break;
        sleep_until(t0);
    }
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) mesh_prog_sampler_reset(&progs[s]);
    free(progs);
    free(prev);
    free(h);
    free(der);
    free(recs);
    free(said);
}

void mesh_net_sampler(mesh_shared *sh) {
    mesh_nics nics = {0};
    char *said = NULL;
    uint32_t samples = 0;
    for (;;) {
        int64_t t0 = mesh_mono_ns();
        mesh_nic_raw *raw;
        size_t nraw;
        char *err = NULL;
        if (mesh_nic_read(&raw, &nraw, &err) == 0) {
            if (said) { mesh_info("net: interface counters readable again"); free(said); said = NULL; }
            mesh_nic_now *now;
            size_t nnow;
            mesh_nics_sample(&nics, raw, nraw, &now, &nnow);
            free(raw);
            uint8_t *h = mesh_xmalloc(MESH_SNAP_HOST);
            mesh_host_block_bytes(&sh->host, h);
            /* The interfaces that carry (or carried) a link, and which links (sorted by name, as the Rust's BTreeMap). */
            char names[MESH_MAX_SYSTEMS][17];
            uint32_t masks[MESH_MAX_SYSTEMS];
            size_t non = 0;
            for (uint32_t k = 0; k < MESH_MAX_SYSTEMS; k++) {
                const uint8_t *b = h + mesh_link(k) + MESH_L_IFACE;
                char nm[17];
                size_t len = 0;
                while (len < 16 && b[len]) len++;
                memcpy(nm, b, len);
                nm[len] = 0;
                if (!len) continue;
                size_t i = 0;
                while (i < non && strcmp(names[i], nm) != 0) i++;
                if (i == non) { strcpy(names[non], nm); masks[non] = 0; non++; }
                masks[i] |= 1u << k;
            }
            free(h);
            for (size_t i = 1; i < non; i++)
                for (size_t j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
                    char t[17];
                    strcpy(t, names[j]);
                    strcpy(names[j], names[j - 1]);
                    strcpy(names[j - 1], t);
                    uint32_t mm = masks[j];
                    masks[j] = masks[j - 1];
                    masks[j - 1] = mm;
                }
            uint8_t recs[MESH_MAX_NICS][MESH_NIC_BYTES];
            size_t nrec = 0;
            static const uint64_t zero[8];
            for (size_t i = 0; i < non && nrec < MESH_MAX_NICS; i++) {
                const uint64_t *v = mesh_nic_lookup(now, nnow, names[i]);
                if (v) mesh_nic_record(recs[nrec++], names[i], v, masks[i], 1);
                else mesh_nic_record(recs[nrec++], names[i], zero, masks[i], 0); /* gone: NI_COUNT 0 says so */
            }
            free(now);
            samples++;
            uint8_t total[MESH_NIC_BYTES];
            mesh_nic_record(total, "total", nics.total, 0, nics.counted);
            uint64_t up = mesh_host_block_uptime_ms(&sh->host);
            uint64_t at = up ? up : 1;
            uint8_t atb[8], cnt[4], smp[4];
            for (int i = 0; i < 8; i++) atb[i] = (uint8_t)(at >> (8 * i));
            for (int i = 0; i < 4; i++) { cnt[i] = (uint8_t)((uint32_t)nrec >> (8 * i)); smp[i] = (uint8_t)(samples >> (8 * i)); }
            mesh_host_field f[4 + MESH_MAX_NICS] = {{MESH_S_NET_TOTAL, total, MESH_NIC_BYTES}, {MESH_S_NET_NICS, cnt, 4}, {MESH_S_NET_SAMPLES, smp, 4},
                                                    {MESH_S_NET_AT_MS, atb, 8}};
            for (size_t i = 0; i < nrec; i++) f[4 + i] = (mesh_host_field){mesh_nic(i), recs[i], MESH_NIC_BYTES};
            mesh_host_block_set_many(&sh->host, f, 4 + nrec);
        } else {
            once(&said, err, "net: cannot read the interface counters");
            free(err);
        }
        if (atomic_load(sh->quit)) break;
        mesh_sleep_us(SAMPLE_NS / 1000);
        (void)t0;
    }
    mesh_nics_free(&nics);
    free(said);
}
