/* load.rs's proc_stat, cpu_rates, classes, psi_and_meminfo. */
#include "mesh/abi/host_load.h"
#include "mesh/check.h"
#include "mesh/load_proc.h"

#include <stdlib.h>
#include <string.h>

static const char *STAT = "cpu  100 10 50 1000 20 5 5 0 7 0\n"
                          "cpu0 50 5 25 500 10 2 3 0 7 0\n"
                          "cpu2 50 5 25 500 10 3 2 0 0 0\n"
                          "intr 12345\nctxt 99\n";

int main(void) {
    static mesh_cpu_times per[MESH_MAX_CPUS];
    static bool have[MESH_MAX_CPUS];
    mesh_cpu_times all;
    size_t n;
    char *err = NULL;
    CHECK_I(mesh_parse_proc_stat(STAT, &all, per, have, &n, &err), 0);
    CHECK(all.busy == 170 && all.total == 1190 && all.iowait == 20);
    CHECK_U(n, 3);
    CHECK(have[0] && per[0].busy == 85 && per[0].total == 595 && per[0].iowait == 10);
    CHECK(!have[1]);
    CHECK_I(mesh_parse_proc_stat("intr 1\n", &all, per, have, &n, &err), -1);
    CHECK_S(err, "/proc/stat has no cpu line");
    free(err);
    CHECK_I(mesh_parse_proc_stat("cpu 1 2 3\ncpu0 1 2 3 4 5 6 7 8\n", &all, per, have, &n, &err), -1);
    CHECK_S(err, "/proc/stat \"cpu 1 2 3\": 3 fields, want at least 8");
    free(err);
    CHECK_I(mesh_parse_proc_stat("cpu 1 2 3 4 5 6 7 8\n", &all, per, have, &n, &err), -1);
    CHECK_S(err, "/proc/stat lists no cpuN line");
    free(err);

    float b, io;
    mesh_cpu_times a = {100, 1000, 10}, c = {150, 1100, 30}, d = {50, 1100, 0};
    mesh_cpu_times_since(&c, &a, &b, &io);
    CHECK(b == 0.5f && io == 0.2f);
    mesh_cpu_times_since(&a, &a, &b, &io);
    CHECK(b == 0 && io == 0);
    mesh_cpu_times_since(&d, &a, &b, &io);
    CHECK(b == 0 && io == 0);

    uint64_t gb10[6] = {718, 731, 997, 1017, 1024, 0};
    bool gh[6] = {1, 1, 1, 1, 1, 0};
    uint8_t cls[6];
    mesh_classify(gb10, gh, 6, cls);
    const uint8_t want[6] = {MESH_CORE_LITTLE, MESH_CORE_LITTLE, MESH_CORE_BIG, MESH_CORE_BIG, MESH_CORE_BIG, MESH_CORE_UNKNOWN};
    CHECK(memcmp(cls, want, 6) == 0);
    uint64_t ryzen[3] = {4951533, 4951533, 4951533};
    bool rh[3] = {1, 1, 1};
    mesh_classify(ryzen, rh, 3, cls);
    CHECK(cls[0] == MESH_CORE_BIG && cls[2] == MESH_CORE_BIG);
    bool none[2] = {0, 0};
    mesh_classify(ryzen, none, 2, cls);
    CHECK(cls[0] == MESH_CORE_UNKNOWN && cls[1] == MESH_CORE_UNKNOWN);
    float util[4] = {1.0f, 0.5f, 0.0f, 0};
    bool uh[4] = {1, 1, 1, 0};
    uint8_t uc[4] = {MESH_CORE_BIG, MESH_CORE_LITTLE, MESH_CORE_BIG, MESH_CORE_BIG};
    float al, bg, lt;
    mesh_class_util(util, uh, uc, 4, &al, &bg, &lt);
    CHECK(al == 0.5f && bg == 0.5f && lt == 0.5f);
    uint8_t allbig[4] = {1, 1, 1, 1};
    mesh_class_util(util, uh, allbig, 4, &al, &bg, &lt);
    CHECK(lt == 0.0f);

    mesh_psi p;
    CHECK_I(mesh_parse_psi("some avg10=1.79 avg60=1.40 avg300=1.21 total=18570277423\nfull avg10=0.00 avg60=0.00 avg300=0.00 total=0\n", &p, &err), 0);
    CHECK(p.some_total == 18570277423ull && p.full_total == 0);
    CHECK(p.some_avg10 > 0.0178f && p.some_avg10 < 0.0180f);
    CHECK_I(mesh_parse_psi("some avg10=1.0 total=5\n", &p, &err), 0);
    CHECK_I(mesh_parse_psi("full avg10=0 total=1\n", &p, &err), -1);
    CHECK_S(err, "PSI: no `some` line");
    free(err);
    CHECK_I(mesh_parse_psi("some avg10=x total=1\n", &p, &err), -1);
    CHECK_S(err, "PSI line \"some avg10=x total=1\": no avg10/total");
    free(err);
    CHECK(mesh_psi_rate(1250000, 1000000, 1000000) == 0.25f);
    CHECK(mesh_psi_rate(5, 10, 1000000) == 0.0f);
    CHECK(mesh_psi_rate(10, 0, 0) == 0.0f);
    mesh_meminfo m;
    CHECK_I(mesh_parse_meminfo("MemTotal:       126000000 kB\nMemFree: 1 kB\nMemAvailable:   90000000 kB\nSwapTotal: 1000 kB\nSwapFree: 250 kB\n", &m, &err), 0);
    CHECK(m.total_kb == 126000000 && m.avail_kb == 90000000 && m.swap_total_kb == 1000 && m.swap_free_kb == 250);
    CHECK_I(mesh_parse_meminfo("MemTotal: 5 kB\n", &m, &err), -1);
    CHECK_S(err, "/proc/meminfo: no MemTotal or MemAvailable");
    free(err);
    float la[3];
    CHECK_I(mesh_parse_loadavg("1.50 2.25 3.00 4/1234 5678\n", la, &err), 0);
    CHECK(la[0] == 1.5f && la[1] == 2.25f && la[2] == 3.0f);
    CHECK_I(mesh_parse_loadavg("1.0 2.0\n", la, &err), -1);
    CHECK_S(err, "/proc/loadavg \"1.0 2.0\\n\"");
    free(err);
    uint64_t v;
    CHECK(mesh_proc_field("nr_free 5\npswpin 7\npswpout 9\n", "pswpout", &v) && v == 9);
    CHECK(mesh_proc_field("Name:\tmeshd\nVmRSS:\t  123456 kB\nThreads:\t17\n", "VmRSS", &v) && v == 123456);
    CHECK(mesh_proc_field("Threads:\t17\n", "Threads", &v) && v == 17);
    CHECK(!mesh_proc_field("Threads:\t17\n", "VmRSS", &v));
    return mesh_check_done();
}
