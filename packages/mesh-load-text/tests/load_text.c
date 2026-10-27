/* The text of an empty sample, and of a machine and a wasm program with figures, against what load.rs prints. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "mesh/abi.h"
#include "mesh/check.h"
#include "mesh/load_text.h"
#include "mesh/manifest.h"
#include "mesh/manifest_encode.h"
#include "mesh/prog_table.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    static _Atomic bool quit;
    mesh_mailbox mb = mesh_mailbox_test();
    mesh_shared *sh = mesh_shared_new(mb, "/tmp", false, 0, 1, &quit);
    mesh_buf t = MESH_BUF_INIT;
    mesh_load_text(sh, &t);
    CHECK_S(mesh_buf_str(&t),
            "load samples 0 age_ms - sample_us 0\n"
            "gpu share - share10 - slice_us - gap_us - max_gap_us 0 gaps_s 0.0 observed 0.00 inflate - queue_us 0.0 util_ext -\n"
            "cpu util 0.000 big 0.000 little 0.000 iowait 0.000 cpus 0 big 0 little 0 loadavg 0.00 0.00 0.00\n"
            "cores \n"
            "psi -\n"
            "mem total_mb 0 avail_mb 0 free 0.000 swap_mb 0/0 swap_out_s 0.0 compressed_mb 0 level -\n"
            "meshd cpu 0.000 rss_mb 0.0 threads 0 log_dropped 0\n"
            "exec cpu 0.000 rss_mb 0.0 wasm_lanes 0 workers 0 spawns 0 crashes 0\n"
            "disk free_mb 0 total_mb 0\n"
            "thermal - temp_c -\n");
    /* Two cores, PSI, a wasm program with latencies. */
    mesh_host_block_set_u32(&sh->host, MESH_S_LOAD_FLAGS, MESH_LF_GPU | MESH_LF_PSI | MESH_LF_CLASSES);
    mesh_host_block_set_f32(&sh->host, MESH_S_GPU_SHARE, 0.478f);
    mesh_host_block_set_f32(&sh->host, MESH_S_GPU_INFLATE, 2.0833f);
    mesh_host_block_set_u32(&sh->host, MESH_S_CPUS, 2);
    uint8_t cores[4] = {50, 0xff, 1, 2};
    mesh_host_field fl[2] = {{MESH_S_CORE_UTIL, cores, 2}, {MESH_S_CORE_CLASS, cores + 2, 2}};
    mesh_host_block_set_many(&sh->host, fl, 2);
    mesh_host_block_set_f32(&sh->host, MESH_S_PSI_CPU, 0.3f);
    mesh_manifest m;
    char *err = NULL;
    const char *mt = "name = \"pipesink\"\nabi = 6\nbackends = [\"wasm\"]";
    CHECK(mesh_manifest_parse(mt, strlen(mt), &m, &err));
    mesh_image im;
    memset(&im, 0, sizeof im);
    uint8_t e[MESH_PT_BYTES], x[MESH_PX_BYTES];
    mesh_manifest_encode_entry(&m, &im, e);
    mesh_manifest_encode_ext(&m, x);
    x[MESH_PX_EXEC] = MESH_BACKEND_WASM;
    mesh_pt_write_entry(mb, 2, e, x, MESH_ST_RUNNING);
    size_t p = mesh_pload(2);
    mesh_host_block_set_u32(&sh->host, p + MESH_PL_FLAGS, MESH_PLF_LOADED | MESH_PLF_LAT | MESH_PLF_HOST);
    mesh_host_block_set_f32(&sh->host, p + MESH_PL_P50_US, 1500.0f);
    mesh_host_block_set_u32(&sh->host, p + MESH_PL_LAT_N, 10);
    mesh_host_block_set_u64(&sh->host, p + MESH_PL_RSS, 3u << 20);
    mesh_host_block_set_u32(&sh->host, p + MESH_PL_UNITS, 2);
    mesh_buf_clear(&t);
    mesh_load_text(sh, &t);
    const char *s = mesh_buf_str(&t);
    CHECK(strstr(s, "gpu share 0.478 share10 0.000 slice_us 0 gap_us 0 max_gap_us 0 gaps_s 0.0 observed 0.00 inflate 2.08 ") != NULL);
    CHECK(strstr(s, "cores B50 L-\n") != NULL);
    CHECK(strstr(s, "psi cpu 0.3000 mem 0.0000 mem_full 0.0000 io 0.0000 io_full 0.0000 avg10 cpu 0.0000 mem 0.0000 io 0.0000\n") != NULL);
    CHECK(strstr(s, "program pipesink slot 2 share - share10 - slice_us - gap_us - max_gap_us 0 observed 0.000 p50_us 1500.0 p90_us 0.0 p99_us 0.0 "
                    "max_us 0.0 lat_n 10 task_us - tasks_s 0.0 busy 0.000 queue_us 0.0 exec wasm cpu 0.000 cpu10 0.000 cpu_task_us 0.0 cpu_ms 0 "
                    "rss_mb 3.0 units 2\n") != NULL);
    mesh_manifest_free(&m);
    mesh_buf_free(&t);
    mesh_shared_free(sh);
    free(mb.base);
    return mesh_check_done();
}
