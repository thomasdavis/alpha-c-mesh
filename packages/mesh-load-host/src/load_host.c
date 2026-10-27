/* The host sample (Linux readers), rates, the machine region and the member summary. */
#include "mesh/load_host.h"
#include "mesh/abi/backend.h"
#include "mesh/buf.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/statvfs.h>

/* f32::round of a value >= 0, without libm. */
static float roundf_pos(float x) {
    float t = (float)(uint32_t)x;
    return x - t >= 0.5f ? t + 1 : t;
}

static void putf(uint8_t *b, size_t off, float v) {
    uint32_t u;
    memcpy(&u, &v, 4);
    for (int i = 0; i < 4; i++) b[off + (size_t)i] = (uint8_t)(u >> (8 * i));
}
static void putu(uint8_t *b, size_t off, uint32_t v) {
    for (int i = 0; i < 4; i++) b[off + (size_t)i] = (uint8_t)(v >> (8 * i));
}
static void putu64(uint8_t *b, size_t off, uint64_t v) {
    for (int i = 0; i < 8; i++) b[off + (size_t)i] = (uint8_t)(v >> (8 * i));
}
static uint32_t getu(const uint8_t *b, size_t off) {
    return (uint32_t)b[off] | (uint32_t)b[off + 1] << 8 | (uint32_t)b[off + 2] << 16 | (uint32_t)b[off + 3] << 24;
}
static uint64_t getu64(const uint8_t *b, size_t off) { return (uint64_t)getu(b, off) | (uint64_t)getu(b, off + 4) << 32; }

/* A whole file as text, or NULL with *err ("<p>: <io error>"). */
static char *slurp(const char *p, char **err) {
    FILE *f = fopen(p, "r");
    if (!f) {
        if (err) *err = mesh_buf_fmt("%s: %s (os error %d)", p, strerror(errno), errno);
        return NULL;
    }
    mesh_buf b = MESH_BUF_INIT;
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, f)) > 0) mesh_buf_put(&b, chunk, got);
    fclose(f);
    mesh_buf_reserve(&b, 0);
    return mesh_buf_take(&b);
}

const char *mesh_core_classes(uint8_t *out, size_t n) {
    static const char *const files[2] = {"cpu_capacity", "cpufreq/cpuinfo_max_freq"};
    static const char *const what[2] = {"cpu_capacity", "cpuinfo_max_freq"};
    uint64_t *v = mesh_xcalloc(n, sizeof *v);
    bool *have = mesh_xcalloc(n, sizeof *have);
    for (int f = 0; f < 2; f++) {
        bool any = false;
        for (size_t i = 0; i < n; i++) {
            char p[128];
            snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%zu/%s", i, files[f]);
            char *t = slurp(p, NULL);
            have[i] = false;
            if (t) {
                char *s = t, *e = t + strlen(t);
                while (*s == ' ' || (*s >= 9 && *s <= 13)) s++;
                while (e > s && (e[-1] == ' ' || (e[-1] >= 9 && e[-1] <= 13))) e--;
                char *end;
                errno = 0;
                unsigned long long x = s < e && *s >= '0' && *s <= '9' ? strtoull(s, &end, 10) : 0;
                if (s < e && *s >= '0' && *s <= '9' && errno == 0 && end == e) { v[i] = x; have[i] = true; any = true; }
                free(t);
            }
        }
        if (any) {
            mesh_classify(v, have, n, out);
            free(v);
            free(have);
            return what[f];
        }
    }
    for (size_t i = 0; i < n; i++) out[i] = MESH_CORE_UNKNOWN;
    free(v);
    free(have);
    return "none";
}

#if defined(__linux__)
bool mesh_has_psi(void) {
    FILE *f = fopen("/proc/pressure/cpu", "r");
    if (!f) return errno != ENOENT && errno != ENOTDIR;
    fclose(f);
    return true;
}

static uint64_t self_cpu_us(void) {
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return (uint64_t)r.ru_utime.tv_sec * 1000000 + (uint64_t)r.ru_utime.tv_usec + (uint64_t)r.ru_stime.tv_sec * 1000000 +
           (uint64_t)r.ru_stime.tv_usec;
}

/* blocks x size / 2^20 in 128 bits (as the Rust's u128), clamped to u32. */
static uint32_t mib(uint64_t a, uint64_t b) {
    uint64_t al = a & 0xffffffffu, ah = a >> 32, bl = b & 0xffffffffu, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (lh & 0xffffffffu) + (hl & 0xffffffffu);
    uint64_t lo = (ll & 0xffffffffu) | mid << 32;
    uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    uint64_t q_hi = hi >> 20, q_lo = lo >> 20 | hi << 44;
    return q_hi || q_lo > UINT32_MAX ? UINT32_MAX : (uint32_t)q_lo;
}

static int disk(const char *dir, uint32_t *free_mb, uint32_t *total_mb, char **err) {
    struct statvfs v;
    if (statvfs(dir, &v) != 0) { *err = mesh_buf_fmt("statvfs %s: %s (os error %d)", dir, strerror(errno), errno); return -1; }
    *free_mb = mib((uint64_t)v.f_bavail, (uint64_t)v.f_frsize);
    *total_mb = mib((uint64_t)v.f_blocks, (uint64_t)v.f_frsize);
    return 0;
}

int mesh_host_read(mesh_host_sample *h, const char *dir, bool psi_ok, char **err) {
    memset(h, 0, sizeof *h);
    char *t = slurp("/proc/stat", err);
    if (!t) return -1;
    int r = mesh_parse_proc_stat(t, &h->cpu_all, h->cpus, h->have, &h->ncpus, err);
    free(t);
    if (r) return -1;
    if (!(t = slurp("/proc/meminfo", err))) return -1;
    mesh_meminfo m;
    r = mesh_parse_meminfo(t, &m, err);
    free(t);
    if (r) return -1;
    if (psi_ok) {
        static const char *const files[3] = {"/proc/pressure/cpu", "/proc/pressure/memory", "/proc/pressure/io"};
        for (int i = 0; i < 3; i++) {
            if (!(t = slurp(files[i], err))) return -1;
            r = mesh_parse_psi(t, &h->psi[i], err);
            free(t);
            if (r) return -1;
        }
        h->has_psi = true;
    }
    char *st = slurp("/proc/self/status", err);
    if (!st) return -1;
    if (disk(dir, &h->disk_free_mb, &h->disk_total_mb, err)) { free(st); return -1; }
    /* The hottest thermal zone. */
    DIR *d = opendir("/sys/class/thermal");
    if (d) {
        struct dirent *e;
        long long best = 0;
        bool any = false;
        while ((e = readdir(d))) {
            if (strncmp(e->d_name, "thermal_zone", 12) != 0) continue;
            char p[300];
            snprintf(p, sizeof p, "/sys/class/thermal/%s/temp", e->d_name);
            char *tt = slurp(p, NULL);
            if (!tt) continue;
            char *s = tt, *end;
            while (*s == ' ' || *s == '\t') s++;
            errno = 0;
            long long x = strtoll(s, &end, 10);
            while (*end == '\n' || *end == ' ' || *end == '\r' || *end == '\t') end++;
            if (end != s && !*end && errno == 0 && (!any || x > best)) { best = x; any = true; }
            free(tt);
        }
        closedir(d);
        if (any) { h->has_temp = true; h->temp_c = (float)best / 1000.0f; }
    }
    if (!(t = slurp("/proc/loadavg", err))) { free(st); return -1; }
    r = mesh_parse_loadavg(t, h->loadavg, err);
    free(t);
    if (r) { free(st); return -1; }
    h->mem_total_mb = (uint32_t)(m.total_kb / 1024);
    h->mem_avail_mb = (uint32_t)(m.avail_kb / 1024);
    h->swap_total_mb = (uint32_t)(m.swap_total_kb / 1024);
    h->swap_used_mb = (uint32_t)((m.swap_total_kb > m.swap_free_kb ? m.swap_total_kb - m.swap_free_kb : 0) / 1024);
    h->mem_free = m.total_kb > 0 ? (float)m.avail_kb / (float)m.total_kb : 0.0f;
    if (!(t = slurp("/proc/vmstat", err))) { free(st); return -1; }
    bool ok = mesh_proc_field(t, "pswpout", &h->swap_out);
    free(t);
    if (!ok) { *err = mesh_xstrdup("/proc/vmstat: no pswpout"); free(st); return -1; }
    h->self_cpu_us = self_cpu_us();
    uint64_t rss, threads;
    if (!mesh_proc_field(st, "VmRSS", &rss)) { *err = mesh_xstrdup("/proc/self/status: no VmRSS"); free(st); return -1; }
    if (!mesh_proc_field(st, "Threads", &threads)) { *err = mesh_xstrdup("/proc/self/status: no Threads"); free(st); return -1; }
    free(st);
    h->self_rss_mb = (float)rss / 1024.0f;
    h->self_threads = (uint32_t)threads;
    return 0;
}
#else
bool mesh_has_psi(void) { return false; }
int mesh_host_read(mesh_host_sample *h, const char *dir, bool psi_ok, char **err) {
    (void)dir, (void)psi_ok;
    memset(h, 0, sizeof *h);
    *err = mesh_xstrdup("load: host counters are read on Linux only in this port");
    return -1;
}
#endif

void mesh_host_derive(const mesh_host_sample *now, const mesh_host_sample *prev, const uint8_t *cls, size_t ncls, double dt,
                      mesh_host_derived *d) {
    memset(d, 0, sizeof *d);
    d->n = now->ncpus;
    for (size_t i = 0; i < now->ncpus; i++) {
        if (!now->have[i] || i >= prev->ncpus || !prev->have[i]) continue;
        float io;
        mesh_cpu_times_since(&now->cpus[i], &prev->cpus[i], &d->util[i], &io);
        d->have_util[i] = true;
    }
    /* class.get(i): a CPU past the class table has no class. */
    uint8_t c[MESH_MAX_CPUS];
    for (size_t i = 0; i < d->n; i++) c[i] = i < ncls ? cls[i] : 0xff;
    float all;
    mesh_class_util(d->util, d->have_util, c, d->n, &all, &d->big, &d->little);
    mesh_cpu_times_since(&now->cpu_all, &prev->cpu_all, &d->all, &d->iowait);
    uint64_t dt_us = (uint64_t)(dt * 1e6);
    if (now->has_psi && prev->has_psi) {
        d->has_psi = true;
        d->psi[0] = mesh_psi_rate(now->psi[0].some_total, prev->psi[0].some_total, dt_us);
        d->psi[1] = mesh_psi_rate(now->psi[1].some_total, prev->psi[1].some_total, dt_us);
        d->psi[2] = mesh_psi_rate(now->psi[1].full_total, prev->psi[1].full_total, dt_us);
        d->psi[3] = mesh_psi_rate(now->psi[2].some_total, prev->psi[2].some_total, dt_us);
        d->psi[4] = mesh_psi_rate(now->psi[2].full_total, prev->psi[2].full_total, dt_us);
    }
    double dts = dt > 1e-3 ? dt : 1e-3;
    d->meshd_cpu = (float)((double)(now->self_cpu_us > prev->self_cpu_us ? now->self_cpu_us - prev->self_cpu_us : 0) / 1e6 / dts);
    d->swap_out_s = (float)((double)(now->swap_out > prev->swap_out ? now->swap_out - prev->swap_out : 0) / dts);
}

void mesh_machine_bytes(uint8_t b[MESH_MACHINE_BYTES], const mesh_host_sample *h, const mesh_host_derived *d, const uint8_t *cls,
                        size_t ncls, const mesh_share *gpu1, const mesh_share *gpu10, uint32_t max_gap_ns, float gaps_s, float obs,
                        uint64_t at_ms, uint32_t samples, uint32_t sample_us) {
#define O(off) ((size_t)(off) - MESH_S_LOAD)
    memset(b, 0, MESH_MACHINE_BYTES);
    uint32_t flags = 0;
    putu64(b, O(MESH_S_LOAD_AT_MS), at_ms);
    putu(b, O(MESH_S_LOAD_SAMPLES), samples);
    bool a_some = gpu1->has_share || gpu10->has_share;
    float a = gpu1->has_share ? gpu1->share : gpu10->share;
    if (a_some && gpu10->has_share) {
        float t = gpu10->share;
        flags |= MESH_LF_GPU;
        putf(b, O(MESH_S_GPU_SHARE), a);
        putf(b, O(MESH_S_GPU_SHARE10), t);
        putf(b, O(MESH_S_GPU_SLICE_US), gpu10->slice_us);
        putf(b, O(MESH_S_GPU_GAP_US), gpu10->gap_us);
        putf(b, O(MESH_S_GPU_QUEUE_US), gpu10->queue_us);
        float inf;
        uint32_t ib = 0x7f800000u;
        memcpy(&inf, &ib, 4);
        putf(b, O(MESH_S_GPU_INFLATE), t > 0.0f ? 1.0f / t : inf);
    }
    putf(b, O(MESH_S_GPU_MAX_GAP_US), (float)max_gap_ns / 1e3f);
    putf(b, O(MESH_S_GPU_GAPS_S), gaps_s);
    putf(b, O(MESH_S_GPU_OBS), obs);
    if (h->has_gpu_util) { flags |= MESH_LF_GPU_EXT; putf(b, O(MESH_S_GPU_UTIL_EXT), h->gpu_util); }
    putf(b, O(MESH_S_CPU_UTIL), d->all);
    putf(b, O(MESH_S_CPU_BIG), d->big);
    putf(b, O(MESH_S_CPU_LITTLE), d->little);
    putf(b, O(MESH_S_CPU_IOWAIT), d->iowait);
    for (int i = 0; i < 3; i++) putf(b, O(MESH_S_LOADAVG) + 4 * (size_t)i, h->loadavg[i]);
    size_t n = h->ncpus < MESH_MAX_CPUS ? h->ncpus : MESH_MAX_CPUS;
    size_t m = n < ncls ? n : ncls;
    uint32_t nb = 0, nl = 0, online = 0;
    for (size_t i = 0; i < m; i++) { nb += cls[i] == MESH_CORE_BIG; nl += cls[i] == MESH_CORE_LITTLE; }
    for (size_t i = 0; i < h->ncpus; i++) online += h->have[i];
    if (nb + nl > 0) flags |= MESH_LF_CLASSES;
    putu(b, O(MESH_S_CPUS), online);
    putu(b, O(MESH_S_CPUS_BIG), nb);
    putu(b, O(MESH_S_CPUS_LITTLE), nl);
    if (d->has_psi && h->has_psi) {
        flags |= MESH_LF_PSI;
        const size_t offs[5] = {MESH_S_PSI_CPU, MESH_S_PSI_MEM, MESH_S_PSI_MEM_FULL, MESH_S_PSI_IO, MESH_S_PSI_IO_FULL};
        for (int i = 0; i < 5; i++) putf(b, O(offs[i]), d->psi[i]);
        const size_t avg[3] = {MESH_S_PSI_CPU10, MESH_S_PSI_MEM10, MESH_S_PSI_IO10};
        for (int i = 0; i < 3; i++) putf(b, O(avg[i]), h->psi[i].some_avg10);
    }
    putu(b, O(MESH_S_MEM_TOTAL_MB), h->mem_total_mb);
    putu(b, O(MESH_S_MEM_AVAIL_MB), h->mem_avail_mb);
    putu(b, O(MESH_S_SWAP_TOTAL_MB), h->swap_total_mb);
    putu(b, O(MESH_S_SWAP_USED_MB), h->swap_used_mb);
    putu(b, O(MESH_S_MEM_COMPRESSED_MB), h->compressed_mb);
    if (h->has_mem_level) { flags |= MESH_LF_MEM_LEVEL; putu(b, O(MESH_S_MEM_LEVEL), h->mem_level); }
    putf(b, O(MESH_S_MEM_FREE), h->mem_free);
    putf(b, O(MESH_S_SWAP_OUT_S), d->swap_out_s);
    putf(b, O(MESH_S_MESHD_CPU), d->meshd_cpu);
    putf(b, O(MESH_S_MESHD_RSS_MB), h->self_rss_mb);
    putu(b, O(MESH_S_MESHD_THREADS), h->self_threads);
    putu(b, O(MESH_S_DISK_FREE_MB), h->disk_free_mb);
    putu(b, O(MESH_S_DISK_TOTAL_MB), h->disk_total_mb);
    if (h->has_thermal) { flags |= MESH_LF_THERMAL; putu(b, O(MESH_S_THERMAL), h->thermal); }
    if (h->has_temp) { flags |= MESH_LF_TEMP; putf(b, O(MESH_S_TEMP_C), h->temp_c); }
    putu(b, O(MESH_S_SAMPLE_US), sample_us);
    putu(b, O(MESH_S_LOAD_FLAGS), flags);
    for (size_t i = 0; i < n; i++) {
        uint8_t u = 0xff;
        if (i < d->n && d->have_util[i]) {
            float p = roundf_pos(d->util[i] * 100.0f);
            u = (uint8_t)(p < 0 ? 0 : p > 100 ? 100 : p);
        }
        b[O(MESH_S_CORE_UTIL) + i] = u;
        b[O(MESH_S_CORE_CLASS) + i] = i < ncls ? cls[i] : MESH_CORE_UNKNOWN;
    }
#undef O
}

void mesh_exec_totals(uint8_t m[MESH_MACHINE_BYTES], const uint8_t (*recs)[MESH_PLOAD_BYTES], const uint32_t *execs, size_t n,
                      uint32_t workers, uint32_t spawns, uint32_t crashes, uint64_t log_dropped) {
#define O(off) ((size_t)(off) - MESH_S_LOAD)
    float cpu = 0;
    uint64_t rss = 0;
    uint32_t lanes = 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *r = recs[i];
        if (!(getu(r, MESH_PL_FLAGS) & MESH_PLF_HOST)) continue;
        uint32_t cb = getu(r, MESH_PL_CPU);
        float c;
        memcpy(&c, &cb, 4);
        cpu += c;
        rss += getu64(r, MESH_PL_RSS);
        if (execs[i] == MESH_BACKEND_WASM) lanes += getu(r, MESH_PL_UNITS);
    }
    putf(m, O(MESH_S_EXEC_CPU), cpu);
    putu(m, O(MESH_S_WASM_LANES), lanes);
    putu64(m, O(MESH_S_EXEC_RSS), rss);
    putu(m, O(MESH_S_WORKERS), workers);
    putu(m, O(MESH_S_WORKER_SPAWNS), spawns);
    putu(m, O(MESH_S_WORKER_CRASHES), crashes);
    putu(m, O(MESH_S_LOG_DROPPED), log_dropped > UINT32_MAX ? UINT32_MAX : (uint32_t)log_dropped);
#undef O
}

void mesh_loadrep(uint8_t b[MESH_LOADREP_BYTES], const uint8_t m[MESH_MACHINE_BYTES]) {
    static const size_t pairs[13][2] = {
        {MESH_ML_SHARE, MESH_S_GPU_SHARE},       {MESH_ML_SHARE10, MESH_S_GPU_SHARE10},
        {MESH_ML_MAX_GAP_US, MESH_S_GPU_MAX_GAP_US}, {MESH_ML_CPU, MESH_S_CPU_UTIL},
        {MESH_ML_CPU_BIG, MESH_S_CPU_BIG},       {MESH_ML_CPU_LITTLE, MESH_S_CPU_LITTLE},
        {MESH_ML_PSI_CPU, MESH_S_PSI_CPU},       {MESH_ML_PSI_MEM, MESH_S_PSI_MEM},
        {MESH_ML_MESHD_CPU, MESH_S_MESHD_CPU},   {MESH_ML_MEM_AVAIL_MB, MESH_S_MEM_AVAIL_MB},
        {MESH_ML_MEM_TOTAL_MB, MESH_S_MEM_TOTAL_MB}, {MESH_ML_DISK_FREE_MB, MESH_S_DISK_FREE_MB},
        {MESH_ML_FLAGS, MESH_S_LOAD_FLAGS}};
    memset(b, 0, MESH_LOADREP_BYTES);
    for (int i = 0; i < 13; i++) memcpy(b + pairs[i][0] - MESH_ML_SHARE, m + pairs[i][1] - MESH_S_LOAD, 4);
    uint32_t cpus = getu(m, MESH_S_CPUS_BIG - MESH_S_LOAD) | getu(m, MESH_S_CPUS_LITTLE - MESH_S_LOAD) << 16;
    putu(b, MESH_ML_CPUS - MESH_ML_SHARE, cpus);
}
