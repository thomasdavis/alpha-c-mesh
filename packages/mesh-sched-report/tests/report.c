/* sched_tests.rs reports_round_trip, with the exact text. */
#include "mesh/check.h"
#include "mesh/sched_report.h"

#include <stdlib.h>

int main(void) {
    msr_report r = {.t_us = 123456789, .cpus = 20, .big = 10, .little = 10, .ram_mb = 121000, .disk_free_mb = 500000, .heap_kb = 6000, .slots_free = 29};
    msm_perf tick = {.hash = 0xabc, .exec = "wasm", .run = true, .task_us = {true, 12.5f}, .tasks_s = 50.0f, .cpu = 0.25f, .rss_mb = 64.0f};
    msm_perf hello = {.hash = 1, .exec = "cuda", .share = {true, 0.48f}};
    msr_add_prog(&r, "tick", &tick);
    msr_add_prog(&r, "hello", &hello);
    msr_add_fail(&r, "tick-big", 0xc3, false, "load tick-big: view heap: 1792 pages wanted, 1789 free");
    msr_add_fail(&r, "tick-m", 7, true, "unsupported");
    msr_add_rtt(&r, 1, 250.5f);
    msr_add_rtt(&r, 3, 1900.0f);
    msr_add_echo(&r, 1, 99, 12);
    msr_add_echo(&r, 3, 1000, 7);
    char *e = msr_encode(&r);
    CHECK_S(e, "t 123456789\n"
               "caps cpus 20 big 10 little 10 ram_mb 121000 disk_mb 500000 heap_kb 6000 slots_free 29\n"
               "prog tick hash 0000000000000abc exec wasm run 1 task_us 12.50 tasks_s 50.00 cpu 0.250 share - rss_mb 64.0\n"
               "prog hello hash 0000000000000001 exec cuda run 0 task_us - tasks_s 0.00 cpu 0.000 share 0.48 rss_mb 0.0\n"
               "rtt 1 250.5\nrtt 3 1900.0\necho 1 99 12\necho 3 1000 7\n"
               "fail tick-big 00000000000000c3 0 load tick-big: view heap: 1792 pages wanted, 1789 free\n"
               "fail tick-m 0000000000000007 1 unsupported\n");
    msr_report d;
    char *err = NULL;
    CHECK(msr_decode(e, &d, &err));
    CHECK(msr_eq(&d, &r));
    msr_free(&d);
    free(e);
    msr_free(&r);
    CHECK(!msr_decode("prog x", &d, &err));
    CHECK_S(err, "F_PERF: \"prog x\"");
    free(err);
    CHECK(msr_decode("fail p 0a 1", &d, &err) && d.nfails == 1 && strcmp(d.fails[0].why, "") == 0);
    msr_free(&d);
    return mesh_check_done();
}
