/* load.rs text(), field by field. */
#include "mesh/load_text.h"
#include "mesh/abi.h"
#include "mesh/prog_table.h"
#include "mesh/rsfmt.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t *H;
static uint32_t u(size_t o) { return (uint32_t)H[o] | (uint32_t)H[o + 1] << 8 | (uint32_t)H[o + 2] << 16 | (uint32_t)H[o + 3] << 24; }
static uint64_t u64(size_t o) { return (uint64_t)u(o) | (uint64_t)u(o + 4) << 32; }
static float f(size_t o) { uint32_t x = u(o); float y; memcpy(&y, &x, 4); return y; }

/* `{:.p}` of an f32 when on, else "-". */
static void opt(mesh_buf *b, bool on, float v, int p) {
    if (on) mesh_rs_fixed(b, v, p);
    else mesh_buf_putc(b, '-');
}
static void fx(mesh_buf *b, float v, int p) { mesh_rs_fixed(b, v, p); }

void mesh_load_text(mesh_shared *sh, mesh_buf *out) {
    uint8_t *hb = malloc(MESH_SNAP_HOST);
    mesh_host_block_bytes(&sh->host, hb);
    H = hb;
    uint32_t flags = u(MESH_S_LOAD_FLAGS);
    uint64_t at = u64(MESH_S_LOAD_AT_MS);
    mesh_buf_printf(out, "load samples %u age_ms ", u(MESH_S_LOAD_SAMPLES));
    if (at == 0) mesh_buf_putc(out, '-');
    else {
        uint64_t up = mesh_host_block_uptime_ms(&sh->host);
        mesh_buf_printf(out, "%llu", (unsigned long long)(up > at ? up - at : 0));
    }
    mesh_buf_printf(out, " sample_us %u\n", u(MESH_S_SAMPLE_US));
    bool g = flags & MESH_LF_GPU;
    mesh_buf_puts(out, "gpu share ");
    opt(out, g, f(MESH_S_GPU_SHARE), 3);
    mesh_buf_puts(out, " share10 ");
    opt(out, g, f(MESH_S_GPU_SHARE10), 3);
    mesh_buf_puts(out, " slice_us ");
    opt(out, g, f(MESH_S_GPU_SLICE_US), 0);
    mesh_buf_puts(out, " gap_us ");
    opt(out, g, f(MESH_S_GPU_GAP_US), 0);
    mesh_buf_puts(out, " max_gap_us ");
    fx(out, f(MESH_S_GPU_MAX_GAP_US), 0);
    mesh_buf_puts(out, " gaps_s ");
    fx(out, f(MESH_S_GPU_GAPS_S), 1);
    mesh_buf_puts(out, " observed ");
    fx(out, f(MESH_S_GPU_OBS), 2);
    mesh_buf_puts(out, " inflate ");
    opt(out, g, f(MESH_S_GPU_INFLATE), 2);
    mesh_buf_puts(out, " queue_us ");
    fx(out, f(MESH_S_GPU_QUEUE_US), 1);
    mesh_buf_puts(out, " util_ext ");
    opt(out, flags & MESH_LF_GPU_EXT, f(MESH_S_GPU_UTIL_EXT), 2);
    mesh_buf_puts(out, "\ncpu util ");
    fx(out, f(MESH_S_CPU_UTIL), 3);
    mesh_buf_puts(out, " big ");
    fx(out, f(MESH_S_CPU_BIG), 3);
    mesh_buf_puts(out, " little ");
    fx(out, f(MESH_S_CPU_LITTLE), 3);
    mesh_buf_puts(out, " iowait ");
    fx(out, f(MESH_S_CPU_IOWAIT), 3);
    mesh_buf_printf(out, " cpus %u big %u little %u loadavg ", u(MESH_S_CPUS), u(MESH_S_CPUS_BIG), u(MESH_S_CPUS_LITTLE));
    fx(out, f(MESH_S_LOADAVG), 2);
    mesh_buf_putc(out, ' ');
    fx(out, f(MESH_S_LOADAVG + 4), 2);
    mesh_buf_putc(out, ' ');
    fx(out, f(MESH_S_LOADAVG + 8), 2);
    mesh_buf_puts(out, "\ncores ");
    uint32_t n = u(MESH_S_CPUS) < MESH_MAX_CPUS ? u(MESH_S_CPUS) : MESH_MAX_CPUS;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t cl = H[MESH_S_CORE_CLASS + i], p = H[MESH_S_CORE_UTIL + i];
        const char *c = cl == 1 ? "B" : cl == 2 ? "L" : "?";
        if (p == 0xff) mesh_buf_printf(out, "%s%s-", i ? " " : "", c);
        else mesh_buf_printf(out, "%s%s%u", i ? " " : "", c, p);
    }
    mesh_buf_putc(out, '\n');
    if (flags & MESH_LF_PSI) {
        const size_t o[8] = {MESH_S_PSI_CPU, MESH_S_PSI_MEM, MESH_S_PSI_MEM_FULL, MESH_S_PSI_IO, MESH_S_PSI_IO_FULL, MESH_S_PSI_CPU10, MESH_S_PSI_MEM10, MESH_S_PSI_IO10};
        const char *k[8] = {"psi cpu ", " mem ", " mem_full ", " io ", " io_full ", " avg10 cpu ", " mem ", " io "};
        for (int i = 0; i < 8; i++) { mesh_buf_puts(out, k[i]); fx(out, f(o[i]), 4); }
        mesh_buf_putc(out, '\n');
    } else mesh_buf_puts(out, "psi -\n");
    mesh_buf_printf(out, "mem total_mb %u avail_mb %u free ", u(MESH_S_MEM_TOTAL_MB), u(MESH_S_MEM_AVAIL_MB));
    fx(out, f(MESH_S_MEM_FREE), 3);
    mesh_buf_printf(out, " swap_mb %u/%u swap_out_s ", u(MESH_S_SWAP_USED_MB), u(MESH_S_SWAP_TOTAL_MB));
    fx(out, f(MESH_S_SWAP_OUT_S), 1);
    mesh_buf_printf(out, " compressed_mb %u level ", u(MESH_S_MEM_COMPRESSED_MB));
    if (flags & MESH_LF_MEM_LEVEL) mesh_buf_printf(out, "%u", u(MESH_S_MEM_LEVEL));
    else mesh_buf_putc(out, '-');
    mesh_buf_puts(out, "\nmeshd cpu ");
    fx(out, f(MESH_S_MESHD_CPU), 3);
    mesh_buf_puts(out, " rss_mb ");
    fx(out, f(MESH_S_MESHD_RSS_MB), 1);
    mesh_buf_printf(out, " threads %u log_dropped %u\nexec cpu ", u(MESH_S_MESHD_THREADS), u(MESH_S_LOG_DROPPED));
    fx(out, f(MESH_S_EXEC_CPU), 3);
    mesh_buf_puts(out, " rss_mb ");
    mesh_rs_fixed(out, (double)u64(MESH_S_EXEC_RSS) / 1048576.0, 1);
    mesh_buf_printf(out, " wasm_lanes %u workers %u spawns %u crashes %u\n", u(MESH_S_WASM_LANES), u(MESH_S_WORKERS), u(MESH_S_WORKER_SPAWNS),
                    u(MESH_S_WORKER_CRASHES));
    mesh_buf_printf(out, "disk free_mb %u total_mb %u\nthermal ", u(MESH_S_DISK_FREE_MB), u(MESH_S_DISK_TOTAL_MB));
    if (flags & MESH_LF_THERMAL) mesh_buf_puts(out, u(MESH_S_THERMAL) < MESH_THERMAL_STATES_LEN ? MESH_THERMAL_STATES[u(MESH_S_THERMAL)] : "?");
    else mesh_buf_putc(out, '-');
    mesh_buf_puts(out, " temp_c ");
    opt(out, flags & MESH_LF_TEMP, f(MESH_S_TEMP_C), 1);
    mesh_buf_putc(out, '\n');
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        size_t p = mesh_pload(s);
        uint32_t pf = u(p + MESH_PL_FLAGS);
        if (!(pf & MESH_PLF_LOADED)) continue;
        char name[MESH_NAME_BYTES + 1];
        mesh_pt_name_of(sh->mb, s, name);
        bool gg = pf & MESH_PLF_GPU, l = pf & MESH_PLF_LAT;
        mesh_buf_printf(out, "program %s slot %u share ", name, s);
        opt(out, gg, f(p + MESH_PL_SHARE), 3);
        mesh_buf_puts(out, " share10 ");
        opt(out, gg, f(p + MESH_PL_SHARE10), 3);
        mesh_buf_puts(out, " slice_us ");
        opt(out, gg, f(p + MESH_PL_SLICE_US), 0);
        mesh_buf_puts(out, " gap_us ");
        opt(out, gg, f(p + MESH_PL_GAP_US), 0);
        mesh_buf_puts(out, " max_gap_us ");
        fx(out, f(p + MESH_PL_MAX_GAP_US), 0);
        mesh_buf_puts(out, " observed ");
        fx(out, f(p + MESH_PL_OBS), 3);
        mesh_buf_puts(out, " p50_us ");
        opt(out, l, f(p + MESH_PL_P50_US), 1);
        mesh_buf_puts(out, " p90_us ");
        opt(out, l, f(p + MESH_PL_P90_US), 1);
        mesh_buf_puts(out, " p99_us ");
        opt(out, l, f(p + MESH_PL_P99_US), 1);
        mesh_buf_puts(out, " max_us ");
        opt(out, l, f(p + MESH_PL_MAX_US), 1);
        mesh_buf_printf(out, " lat_n %u task_us ", u(p + MESH_PL_LAT_N));
        opt(out, pf & MESH_PLF_TASK, f(p + MESH_PL_TASK_US), 1);
        mesh_buf_puts(out, " tasks_s ");
        fx(out, f(p + MESH_PL_TASKS_S), 1);
        mesh_buf_puts(out, " busy ");
        fx(out, f(p + MESH_PL_BUSY), 3);
        mesh_buf_puts(out, " queue_us ");
        fx(out, f(p + MESH_PL_QUEUE_US), 1);
        if (pf & MESH_PLF_HOST) {
            uint32_t ex = mesh_mailbox_get(sh->mb, mesh_prog_ext(s) + MESH_PX_EXEC);
            mesh_buf_printf(out, " exec %s cpu ", ex < MESH_BACKENDS_LEN ? MESH_BACKENDS[ex] : "?");
            fx(out, f(p + MESH_PL_CPU), 3);
            mesh_buf_puts(out, " cpu10 ");
            fx(out, f(p + MESH_PL_CPU10), 3);
            mesh_buf_puts(out, " cpu_task_us ");
            fx(out, f(p + MESH_PL_CPU_TASK_US), 1);
            mesh_buf_printf(out, " cpu_ms %llu rss_mb ", (unsigned long long)(u64(p + MESH_PL_CPU_NS) / 1000000));
            mesh_rs_fixed(out, (double)u64(p + MESH_PL_RSS) / 1048576.0, 1);
            mesh_buf_printf(out, " units %u", u(p + MESH_PL_UNITS));
        }
        mesh_buf_putc(out, '\n');
    }
    free(hb);
}
