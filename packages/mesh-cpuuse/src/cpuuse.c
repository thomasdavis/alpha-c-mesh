/* cpuuse.rs: per-slot counters and the per-thread meter. */
#include "mesh/cpuuse.h"

#include <time.h>

mesh_slot_use mesh_cpuuse_slots[MESH_PROG_SLOTS];
_Atomic uint32_t mesh_cpuuse_workers, mesh_cpuuse_worker_spawns, mesh_cpuuse_worker_crashes;

void mesh_slot_use_add_cpu(mesh_slot_use *u, uint64_t ns) { atomic_fetch_add_explicit(&u->cpu_ns, ns, memory_order_relaxed); }

void mesh_slot_use_add_rss(mesh_slot_use *u, int64_t d) {
    if (d >= 0) {
        atomic_fetch_add_explicit(&u->rss, (uint64_t)d, memory_order_relaxed);
        return;
    }
    uint64_t sub = (uint64_t)(-d), cur = atomic_load_explicit(&u->rss, memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&u->rss, &cur, cur > sub ? cur - sub : 0, memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

void mesh_slot_use_get(mesh_slot_use *u, uint64_t *cpu_ns, uint64_t *rss, uint32_t *units) {
    *cpu_ns = atomic_load_explicit(&u->cpu_ns, memory_order_relaxed);
    *rss = atomic_load_explicit(&u->rss, memory_order_relaxed);
    *units = atomic_load_explicit(&u->units, memory_order_relaxed);
}

uint64_t mesh_thread_cpu_ns(void) {
    struct timespec ts = {0, 0};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

void mesh_meter_start(mesh_meter *m, uint32_t slot) {
    atomic_fetch_add_explicit(&mesh_cpuuse_slots[slot].units, 1, memory_order_relaxed);
    *m = (mesh_meter){slot, mesh_thread_cpu_ns(), 0};
}

void mesh_meter_tick(mesh_meter *m, uint64_t rss) {
    uint64_t now = mesh_thread_cpu_ns();
    mesh_slot_use *u = &mesh_cpuuse_slots[m->slot];
    mesh_slot_use_add_cpu(u, now > m->cpu ? now - m->cpu : 0);
    m->cpu = now;
    if (rss != m->rss) {
        mesh_slot_use_add_rss(u, (int64_t)rss - (int64_t)m->rss);
        m->rss = rss;
    }
}

void mesh_meter_end(mesh_meter *m) {
    mesh_slot_use *u = &mesh_cpuuse_slots[m->slot];
    uint64_t now = mesh_thread_cpu_ns();
    mesh_slot_use_add_cpu(u, now > m->cpu ? now - m->cpu : 0);
    mesh_slot_use_add_rss(u, -(int64_t)m->rss);
    atomic_fetch_sub_explicit(&u->units, 1, memory_order_relaxed);
}
