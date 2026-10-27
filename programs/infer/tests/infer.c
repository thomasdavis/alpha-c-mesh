/* infer on the simulator: no train loaded -> R_NO_TRAIN; with a train slot
 * (its view written here), every grid point's y == the CPU reference
 * (@ajax/mesh-ref-cubic) bitwise; peer reads are gated by `reads`. */
#include "mesh/check.h"
#include "mesh/ref/cubic.h"
#include "mesh/rt/sim_mailbox.h"

#include "../src/infer.c"

int main(void) {
    uint8_t *mb = mesh_sim_mailbox();
    mesh_sim_prog p = {.name = "infer", .view_off = MESH_VIEW_HEAP, .view_bytes = MESH_INFER_VIEW_BYTES, .reads_mask = 1u << 4};
    uint8_t *view = mesh_sim_install(mb, 1, &p);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, 1, &p, mesh_main);
    CHECK_U(mesh_sim_call(mb, 1, MESH_OP_INIT, 0), MESH_R_NONE);
    float grid[MESH_INFER_POINTS];
    mesh_cubic_grid(MESH_INFER_POINTS, grid);
    for (uint32_t i = 0; i < MESH_INFER_POINTS; i++) CHECK_U(mesh_sim_u32(view + MESH_INFER_I_X + 4 * i), mesh_f32_bits(grid[i]));
    CHECK_U(mesh_sim_call(mb, 1, MESH_OP_TASK, 0), MESH_INFER_R_NO_TRAIN);
    /* A train in slot 4 (readable): its params and step. */
    mesh_sim_prog t = {.name = "train", .view_off = MESH_VIEW_HEAP + 0x10000, .view_bytes = MESH_TRAIN_VIEW_BYTES};
    uint8_t *tv = mesh_sim_install(mb, 4, &t);
    const float params[4] = {0.0123f, 2.75f, -0.03125f, -2.5f};
    for (int k = 0; k < 4; k++) mesh_sim_set_f32(tv + MESH_TRAIN_T_PARAMS + 4 * k, params[k]);
    mesh_sim_set_u32(tv + MESH_TRAIN_T_NEXT_STEP, 640);
    CHECK_U(mesh_sim_call(mb, 1, MESH_OP_TASK, 0), MESH_INFER_POINTS);
    for (uint32_t i = 0; i < MESH_INFER_POINTS; i++)
        CHECK_U(mesh_sim_u32(view + MESH_INFER_I_Y + 4 * i), mesh_f32_bits(mesh_cubic(params, grid[i])));
    for (int k = 0; k < 4; k++) CHECK_U(mesh_sim_u32(view + MESH_INFER_I_PARAMS + 4 * k), mesh_f32_bits(params[k]));
    CHECK_U(mesh_sim_u32(view + MESH_INFER_I_RUNS), 1);
    CHECK_U(mesh_sim_u32(view + MESH_INFER_I_TRAIN_STEP), 640);
    /* Probe reads: slot 4 is in `reads`, slot 2 is not (a fault). */
    CHECK_U(mesh_sim_call(mb, 1, MESH_INFER_OP_PROBE_READ, 4), MESH_TRAIN_VIEW_BYTES);
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, 1) + MESH_P_FAULT), 0);
    CHECK_U(mesh_sim_call(mb, 1, MESH_INFER_OP_PROBE_READ, 2), MESH_INFER_R_NO_VIEW);
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, 1) + MESH_P_FAULT), MESH_FAULT_PEER_VIEW | 2u << 8);
    /* An unloaded train is gone. */
    mesh_sim_set_state(mb, 4, MESH_ST_EMPTY);
    CHECK_U(mesh_sim_call(mb, 1, MESH_OP_TASK, 0), MESH_INFER_R_NO_TRAIN);
    mesh_sim_halt(mb, 1, l);
    mesh_sim_mailbox_free(mb);
    return mesh_check_done();
}
