/* load.rs's machine_region_and_loadrep and this_host_samples. */
#include "mesh/abi/backend.h"
#include "mesh/check.h"
#include "mesh/load_host.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static float f(const uint8_t *m, size_t off) { uint32_t u = (uint32_t)m[off] | (uint32_t)m[off+1] << 8 | (uint32_t)m[off+2] << 16 | (uint32_t)m[off+3] << 24; float x; memcpy(&x, &u, 4); return x; }
static uint32_t u(const uint8_t *m, size_t off) { return (uint32_t)m[off] | (uint32_t)m[off+1] << 8 | (uint32_t)m[off+2] << 16 | (uint32_t)m[off+3] << 24; }

int main(void) {
    static mesh_host_sample h, p, a, b;
    h.cpus[0] = (mesh_cpu_times){100, 200, 0};
    h.cpus[1] = (mesh_cpu_times){0, 200, 0};
    h.have[0] = h.have[1] = true;
    h.ncpus = 3;
    h.cpu_all = (mesh_cpu_times){100, 400, 10};
    h.loadavg[0] = 1; h.loadavg[1] = 2; h.loadavg[2] = 3;
    h.has_psi = true;
    h.psi[0].some_avg10 = 0.01f;
    h.psi[0].some_total = 300000;
    h.mem_total_mb = 1000; h.mem_avail_mb = 600; h.mem_free = 0.6f; h.self_cpu_us = 1270000; h.disk_free_mb = 42;
    p.ncpus = 3;
    for (int i = 0; i < 3; i++) p.have[i] = true;
    p.has_psi = true;
    const uint8_t cls[3] = {MESH_CORE_BIG, MESH_CORE_LITTLE, MESH_CORE_BIG};
    static mesh_host_derived d;
    mesh_host_derive(&h, &p, cls, 3, 1.0, &d);
    CHECK(d.all == 0.25f && d.big == 0.5f && d.little == 0.0f && d.iowait == 0.025f);
    CHECK(d.psi[0] == 0.3f);
    CHECK(fabsf(d.meshd_cpu - 1.27f) < 1e-6f);
    mesh_share g = {true, 0.48f, 2087.0f, 2280.0f, 0.0f};
    uint8_t m[MESH_MACHINE_BYTES];
    mesh_machine_bytes(m, &h, &d, cls, 3, &g, &g, 2480000, 229.0f, 0.9f, 5000, 7, 120);
#define F(o) f(m, (o) - MESH_S_LOAD)
#define U(o) u(m, (o) - MESH_S_LOAD)
    CHECK_U(U(MESH_S_LOAD_FLAGS), MESH_LF_GPU | MESH_LF_PSI | MESH_LF_CLASSES);
    CHECK(F(MESH_S_GPU_SHARE) == 0.48f && F(MESH_S_GPU_MAX_GAP_US) == 2480.0f && F(MESH_S_PSI_CPU) == 0.3f);
    CHECK(fabsf(F(MESH_S_GPU_INFLATE) - 2.0833f) < 1e-3f);
    CHECK(U(MESH_S_CPUS) == 2 && U(MESH_S_CPUS_BIG) == 2 && U(MESH_S_CPUS_LITTLE) == 1);
    const uint8_t util[3] = {50, 0, 0xff};
    CHECK(memcmp(m + MESH_S_CORE_UTIL - MESH_S_LOAD, util, 3) == 0);
    CHECK(memcmp(m + MESH_S_CORE_CLASS - MESH_S_LOAD, cls, 3) == 0);
    uint8_t r[MESH_LOADREP_BYTES];
    mesh_loadrep(r, m);
    CHECK(f(r, MESH_ML_SHARE - MESH_ML_SHARE) == 0.48f && f(r, MESH_ML_CPU - MESH_ML_SHARE) == 0.25f && f(r, MESH_ML_PSI_CPU - MESH_ML_SHARE) == 0.3f);
    CHECK(u(r, MESH_ML_MEM_AVAIL_MB - MESH_ML_SHARE) == 600 && u(r, MESH_ML_DISK_FREE_MB - MESH_ML_SHARE) == 42);
    CHECK_U(u(r, MESH_ML_CPUS - MESH_ML_SHARE), 2 | 1 << 16);
    CHECK_U(u(r, MESH_ML_FLAGS - MESH_ML_SHARE), MESH_LF_GPU | MESH_LF_PSI | MESH_LF_CLASSES);

    /* exec totals: one wasm and one native host record. */
    static uint8_t recs[2][MESH_PLOAD_BYTES];
    const uint32_t execs[2] = {MESH_BACKEND_WASM, 3};
    for (int i = 0; i < 2; i++) {
        uint32_t fl = MESH_PLF_LOADED | MESH_PLF_HOST, units = 3;
        float c = 0.5f;
        memcpy(recs[i] + MESH_PL_FLAGS, &fl, 4);
        memcpy(recs[i] + MESH_PL_CPU, &c, 4);
        memcpy(recs[i] + MESH_PL_UNITS, &units, 4);
        uint64_t rss = 1 << 20;
        memcpy(recs[i] + MESH_PL_RSS, &rss, 8);
    }
    mesh_exec_totals(m, (const uint8_t (*)[MESH_PLOAD_BYTES])recs, execs, 2, 4, 5, 6, 7);
    CHECK(F(MESH_S_EXEC_CPU) == 1.0f && U(MESH_S_WASM_LANES) == 3 && U(MESH_S_WORKERS) == 4 && U(MESH_S_LOG_DROPPED) == 7);

    /* This host. */
    uint8_t c[MESH_MAX_CPUS];
    mesh_core_classes(c, MESH_MAX_CPUS);
    char *err = NULL;
    CHECK_I(mesh_host_read(&a, ".", mesh_has_psi(), &err), 0);
    if (err) { fprintf(stderr, "%s\n", err); free(err); err = NULL; }
    struct timespec ts = {0, 50000000};
    nanosleep(&ts, NULL);
    CHECK_I(mesh_host_read(&b, ".", mesh_has_psi(), &err), 0);
    size_t on = 0;
    for (size_t i = 0; i < b.ncpus; i++) on += b.have[i];
    CHECK(on >= 1 && b.mem_total_mb > 0 && b.disk_total_mb > 0 && b.self_threads >= 1);
    mesh_host_derive(&b, &a, c, MESH_MAX_CPUS, 0.05, &d);
    CHECK(d.all >= 0 && d.all <= 1);
    CHECK(d.meshd_cpu >= 0);
    return mesh_check_done();
}
