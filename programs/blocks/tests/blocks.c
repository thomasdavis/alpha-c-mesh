/* kcheck's B checks on the CPU simulator: every collective of a 32-warp node
 * (B1) and a 4-warp one (B2) bitwise against @ajax/mesh-ref-block, the f32
 * sum of 4 warps equal to the 1024-leaf tree's, and early exits with a
 * stop/start (B3). Ported from alpha-rust-mesh crates/mesh-kcheck/src/blocks.rs. */
#include "mesh/check.h"
#include "mesh/ref/block.h"
#include "mesh/rt/sim_mailbox.h"

#include "../src/blocks.c"

#include <stdlib.h>

static uint32_t f2u(float f) { return mesh_f32_bits(f); }

/* One seed through the slot: header and every thread's words == the reference. */
static void check_seed(uint8_t *mb, uint32_t slot, uint32_t seed, uint32_t threads, uint32_t nodes) {
    float f[1024];
    uint32_t u[1024];
    uint64_t l[1024];
    for (uint32_t t = 0; t < threads; t++) mesh_block_test_values(seed, t, &f[t], &u[t], &l[t]);
    float sf = mesh_block_reduce_f32(f, threads, mesh_block_sum_f32_id(), mesh_block_add_f32);
    float mf = mesh_block_reduce_f32(f, threads, mesh_block_max_f32_id(), mesh_block_max_f32);
    uint32_t su = mesh_block_reduce_u32(u, threads, 0, mesh_block_add_u32), mu = mesh_block_reduce_u32(u, threads, 0, mesh_rt_max_u32);
    uint64_t sl = mesh_block_reduce_u64(l, threads, 0, mesh_block_add_u64), ml = mesh_block_reduce_u64(l, threads, 0, mesh_rt_max_u64);
    static float fi[1024], fe[1024];
    static uint32_t ui[1024], ue[1024];
    static uint64_t li[1024], le[1024];
    CHECK(mesh_block_scan_f32(f, threads, mesh_block_sum_f32_id(), mesh_block_add_f32, true, fi));
    CHECK(mesh_block_scan_f32(f, threads, mesh_block_sum_f32_id(), mesh_block_add_f32, false, fe));
    CHECK(mesh_block_scan_u32(u, threads, 0, mesh_block_add_u32, true, ui));
    CHECK(mesh_block_scan_u32(u, threads, 0, mesh_block_add_u32, false, ue));
    CHECK(mesh_block_scan_u64(l, threads, 0, mesh_block_add_u64, true, li));
    CHECK(mesh_block_scan_u64(l, threads, 0, mesh_block_add_u64, false, le));

    uint32_t t = mesh_sim_publish(mb, slot, MESH_OP_TASK, seed), res = 0;
    CHECK(mesh_sim_wait(mb, slot, t, &res, 120000));
    uint8_t *view = mesh_sim_view(mb, slot);
    uint32_t r = BLOCKS_R0 + BLOCKS_REGION * (t % 4);
    uint32_t hdr[10] = {threads, f2u(sf), f2u(mf), su, mu, (uint32_t)sl, (uint32_t)(sl >> 32), (uint32_t)ml, (uint32_t)(ml >> 32), u[seed % threads]};
    for (uint32_t i = 0; i < 10; i++) CHECK_U(mesh_sim_u32(view + r + 4 * i), hdr[i]);
    CHECK(mesh_sim_u32(view + r + 40) < nodes);
    CHECK_U(mesh_sim_u32(view + r + 44), threads / 32);
    CHECK_U(res, su);
    uint32_t wrong = 0;
    for (uint32_t th = 0; th < threads; th++) {
        uint32_t want[9] = {f2u(fi[th]), f2u(fe[th]), ui[th], ue[th], (uint32_t)li[th], (uint32_t)(li[th] >> 32), (uint32_t)le[th],
                            (uint32_t)(le[th] >> 32), f2u(f[threads - 1 - th])};
        for (uint32_t i = 0; i < 9; i++)
            if (mesh_sim_u32(view + r + BLOCKS_HDR + BLOCKS_PER * th + 4 * i) != want[i]) wrong++;
    }
    CHECK_U(wrong, 0);
}

static void run_shape(const char *name, uint32_t warps, uint32_t nodes, uint32_t shared, const uint32_t *seeds, size_t ns) {
    uint8_t *mb = mesh_sim_mailbox();
    mesh_sim_prog p = {.name = name, .view_bytes = 0x41000, .nodes = nodes, .warps = warps, .shared_bytes = shared};
    mesh_sim_install(mb, 0, &p);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, 0, &p, mesh_main);
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_INIT, 0), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(mesh_sim_view(mb, 0)), BLOCKS_MAGIC);
    for (size_t i = 0; i < ns; i++) check_seed(mb, 0, seeds[i], 32 * warps, nodes);
    if (warps == 32) {
        /* B3: early exits (warp 0 alone sums its lanes: 496), then stop/start and a task. */
        uint8_t *ctl = mesh_sim_ctl(mb, 0);
        uint32_t c0 = mesh_sim_u32(ctl + MESH_P_COUNTERS + 4);
        for (int round = 0; round < 2; round++) {
            uint32_t ts[32];
            for (int k = 0; k < 32; k++) ts[k] = mesh_sim_publish(mb, 0, BLOCKS_OP_EARLY, 0);
            for (int k = 0; k < 32; k++) {
                uint32_t r = 0;
                CHECK(mesh_sim_wait(mb, 0, ts[k], &r, 60000));
                CHECK_U(r, 496);
            }
        }
        CHECK_U(mesh_sim_u32(ctl + MESH_P_COUNTERS + 4), c0 + 64);
        mesh_sim_halt(mb, 0, l);
        l = mesh_sim_launch_prog(mb, 0, &p, mesh_main);
        uint32_t u[1024], s = 0;
        float f;
        uint64_t w;
        for (uint32_t t = 0; t < 1024; t++) { mesh_block_test_values(5, t, &f, &u[t], &w); }
        s = mesh_block_reduce_u32(u, 1024, 0, mesh_block_add_u32);
        CHECK_U(mesh_sim_call(mb, 0, MESH_OP_TASK, 5), s);
    } else {
        /* Fewer warps: the tree is the 1024-leaf one, padded with -0.0 (the same f32 sum). */
        float f[1024];
        uint32_t u;
        uint64_t w;
        for (uint32_t t = 0; t < 1024; t++) {
            if (t < 32 * warps) mesh_block_test_values(seeds[0], t, &f[t], &u, &w);
            else f[t] = mesh_block_sum_f32_id();
        }
        CHECK_U(f2u(mesh_block_reduce_f32(f, 32 * warps, mesh_block_sum_f32_id(), mesh_block_add_f32)),
                f2u(mesh_block_reduce_f32(f, 1024, mesh_block_sum_f32_id(), mesh_block_add_f32)));
    }
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, 0) + MESH_P_FAULT), 0);
    mesh_sim_halt(mb, 0, l);
    mesh_sim_mailbox_free(mb);
}

int main(void) {
    const uint32_t seeds[] = {1, 2, 3, 0xdeadbeefu, mesh_block_mix(100), mesh_block_mix(101)};
    run_shape("blocks", 32, 1, 0x1000, seeds, sizeof seeds / sizeof *seeds);
    run_shape("blocks-w4", 4, 2, 0x200, seeds, sizeof seeds / sizeof *seeds);
    run_shape("blocks-w1", 1, 1, 0x200, seeds, 2);
    return mesh_check_done();
}
