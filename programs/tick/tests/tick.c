/* tick on the simulator: init lays the pattern, ticks overwrite word t mod W,
 * verify finds no mismatch; a corrupted word is found; a relaunch (as after a
 * migration, no OP_INIT) carries on from the view. */
#include "mesh/check.h"
#include "mesh/rt/sim_mailbox.h"

#include "../src/tick.c"

int main(void) {
    uint8_t *mb = mesh_sim_mailbox();
    mesh_sim_prog p = {.name = "tick", .view_bytes = 0x10000};
    uint8_t *view = mesh_sim_install(mb, 2, &p);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, 2, &p, mesh_main);
    CHECK_U(mesh_sim_call(mb, 2, MESH_OP_INIT, 0), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view), TICK_MAGIC);
    CHECK_U(mesh_sim_u32(view + TICK_INITS), 1);
    uint32_t w = (0x10000 - TICK_REGION) / 4;
    for (uint32_t i = 0; i < w; i++)
        if (mesh_sim_u32(view + TICK_REGION + 4 * i) != tick_pattern(i)) { CHECK(0); break; }
    for (uint32_t i = 0; i < 100; i++) mesh_sim_call(mb, 2, MESH_OP_TASK, 0);
    CHECK_U(mesh_sim_u32(view + TICK_TICKS), 100);
    CHECK_U(mesh_sim_call(mb, 2, TICK_OP_VERIFY, 0), 0);
    CHECK_U(mesh_sim_u32(view + TICK_REGION + 4 * 99), tick_pattern(99) ^ 100);
    /* A migration: halt, relaunch without OP_INIT: the ticks continue. */
    mesh_sim_halt(mb, 2, l);
    l = mesh_sim_launch_prog(mb, 2, &p, mesh_main);
    for (uint32_t i = 0; i < 28; i++) mesh_sim_call(mb, 2, MESH_OP_TASK, 0);
    CHECK_U(mesh_sim_u32(view + TICK_TICKS), 128);
    CHECK_U(mesh_sim_u32(view + TICK_INITS), 1);
    CHECK_U(mesh_sim_call(mb, 2, TICK_OP_VERIFY, 0), 0);
    /* A word that is not this view's own (one of the last 64 ticks'). */
    /* Word 126 is one of the last 64 ticks' and not sampled: found once. */
    mesh_sim_set_u32(view + TICK_REGION + 4 * 126, 0);
    CHECK_U(mesh_sim_call(mb, 2, TICK_OP_VERIFY, 0), 1);
    CHECK_U(mesh_sim_u32(view + TICK_MISMATCH), 1);
    /* Word 127 is both (sample 32 lands on it): found twice. */
    mesh_sim_set_u32(view + TICK_REGION + 4 * 127, 0);
    CHECK_U(mesh_sim_call(mb, 2, TICK_OP_VERIFY, 0), 3);
    CHECK_U(mesh_sim_call(mb, 2, 7, 0), MESH_R_BAD_OP);
    CHECK_U(tick_expect(5, 3, 10), tick_pattern(5));
    CHECK_U(tick_expect(5, 6, 10), tick_pattern(5) ^ 6);
    CHECK_U(tick_expect(5, 16, 10), tick_pattern(5) ^ 16);
    mesh_sim_halt(mb, 2, l);
    mesh_sim_mailbox_free(mb);
    return mesh_check_done();
}
