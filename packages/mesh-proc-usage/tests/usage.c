/* native.rs's usage_of_self. */
#include "mesh/check.h"
#include "mesh/proc_usage.h"

#include <unistd.h>

int main(void) {
    uint64_t cpu, rss, cpu2, rss2;
    CHECK(mesh_proc_usage(getpid(), &cpu, &rss));
    volatile uint64_t x = 0;
    for (uint64_t i = 0; i < 200000000u; i++) x += i * i;
    CHECK(mesh_proc_usage(getpid(), &cpu2, &rss2));
    CHECK(rss > 1u << 20 || rss > 100000);
    CHECK(cpu2 >= cpu);
    CHECK(cpu2 > 0);
    CHECK(!mesh_proc_usage(999999999, &cpu, &rss));
    return mesh_check_done();
}
