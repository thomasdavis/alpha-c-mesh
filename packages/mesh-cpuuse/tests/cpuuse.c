/* cpuuse.rs's meter_counts_this_threads_cpu_and_memory. */
#include "mesh/check.h"
#include "mesh/cpuuse.h"

#include <pthread.h>
#include <time.h>

static double ms_since(const struct timespec *t0) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - t0->tv_sec) * 1e3 + (double)(t.tv_nsec - t0->tv_nsec) / 1e6;
}

static void *lane(void *arg) {
    (void)arg;
    mesh_meter m;
    mesh_meter_start(&m, 31);
    uint64_t cpu, rss;
    uint32_t units;
    mesh_slot_use_get(&mesh_cpuuse_slots[31], &cpu, &rss, &units);
    CHECK_U(units, 1);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    volatile uint64_t x = 0;
    while (ms_since(&t0) < 30) x = x * 31 + 7;
    mesh_meter_tick(&m, 1u << 20);
    mesh_slot_use_get(&mesh_cpuuse_slots[31], &cpu, &rss, &units);
    CHECK(cpu >= 20000000);
    CHECK_U(rss, 1u << 20);
    CHECK_U(units, 1);
    mesh_meter_tick(&m, 3u << 20);
    mesh_slot_use_get(&mesh_cpuuse_slots[31], &cpu, &rss, &units);
    CHECK_U(rss, 3u << 20);
    mesh_meter_end(&m);
    return NULL;
}

int main(void) {
    pthread_t t;
    pthread_create(&t, NULL, lane, NULL);
    pthread_join(t, NULL);
    uint64_t cpu, rss;
    uint32_t units;
    mesh_slot_use_get(&mesh_cpuuse_slots[31], &cpu, &rss, &units);
    CHECK(cpu < 200000000);
    CHECK_U(rss, 0);
    CHECK_U(units, 0);
    return mesh_check_done();
}
