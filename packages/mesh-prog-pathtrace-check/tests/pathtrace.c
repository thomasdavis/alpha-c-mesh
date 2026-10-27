/* programs/pathtrace on the simulator against @ajax/mesh-ref-pathtrace (the
 * renderer in Z), bitwise, as kcheck's PT checks: a scene uploaded through
 * the request window (2 x 2 tiles, 3 passes of 2 samples), every item as an
 * OP_JOB in lease order, each tile's accumulators, pixels and digests equal
 * to the reference's; a re-run folds nothing; OP_TONEMAP re-tones to the
 * reference's Reinhard pixels; OP_PASS's payload digest; pathtrace-live's
 * OP_TASK items; a bad scene refused. */
#include "mesh/check.h"
#include "mesh/ref/pathtrace.h"
#include "mesh/rt/sim_mailbox.h"
#include "pathtrace/job_scene.h"

#include "../../../programs/pathtrace/src/pathtrace.c"

#include <stdlib.h>

#define TILES 4u
#define PASSES 3u

static uint8_t *mb, *view;
static uint32_t scene[PT_DEFAULT_SCENE_WORDS];

static void check_tiles(uint32_t op, uint32_t passes) {
    for (uint32_t t = 0; t < TILES; t++) {
        mesh_pt_tile ref;
        CHECK(mesh_pt_tile_render(scene, t, passes, op, &ref));
        uint32_t bad = 0;
        for (uint32_t i = 0; i < 3 * PT_TILE_PIX; i++) bad += mesh_sim_u32(view + pt_view_acc(t) + 4 * i) != ref.acc[i];
        for (uint32_t i = 0; i < PT_TILE_PIX; i++) bad += mesh_sim_u32(view + pt_view_rgb(t) + 4 * i) != ref.rgb[i];
        CHECK_U(bad, 0);
        uint32_t m = pt_view_meta(t);
        CHECK_U(mesh_sim_u32(view + m + PT_M_PASSES), passes);
        CHECK_U(mesh_sim_u64(view + m + PT_M_ACC), ref.acc_digest);
        CHECK_U(mesh_sim_u64(view + m + PT_M_RGB), ref.rgb_digest);
        mesh_pt_tile_free(&ref);
    }
}

int main(void) {
    /* The default scene, 64 x 48, 2 samples a pass, 3 passes. */
    pt_setup_task setup[5] = {{PT_OP_DEFAULT, 0}, {PT_OP_SET, pt_set_arg(PT_H_WIDTH, 64)}, {PT_OP_SET, pt_set_arg(PT_H_HEIGHT, 48)},
                              {PT_OP_SET, pt_set_arg(PT_H_SPP, 2)}, {PT_OP_SET, pt_set_arg(PT_H_PASSES, PASSES)}};
    CHECK_I(pt_job_scene(setup, 5, scene), -1);
    CHECK_U(pt_validate_in(scene, PT_DEFAULT_SCENE_WORDS, PT_MAX_TILES), 0);
    CHECK_U(pt_tiles(scene), TILES);

    mb = mesh_sim_mailbox();
    mesh_sim_prog p = {.name = "pathtrace", .view_bytes = PT_VIEW_BYTES, .nodes = 2, .warps = 24, .shared_bytes = 0x3400,
                       .req_off = PT_V_REQ, .req_max = PT_REQ_MAX, .resp_off = PT_V_RESP, .resp_max = PT_RESP_MAX};
    view = mesh_sim_install(mb, 0, &p);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, 0, &p, mesh_main);
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_INIT, 0), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view + PT_V_MAGIC), PT_VIEW_MAGIC);
    CHECK_U(mesh_sim_u32(view + PT_V_TILES), 300);
    /* The scene through the request window. */
    char out[256];
    uint32_t n = 0;
    CHECK_U(mesh_sim_request(mb, 0, scene, sizeof scene, out, sizeof out - 1, &n), MESH_R_NONE);
    out[n] = 0;
    CHECK_S(out, "ok scene gen 2 tiles 4\n");
    CHECK_U(mesh_sim_u32(view + PT_V_WIDTH), 64);
    /* A bad scene: words not a multiple of 4. */
    CHECK_U(mesh_sim_request(mb, 0, scene, 7, out, sizeof out - 1, &n), MESH_R_BAD_OP);
    out[n] = 0;
    CHECK_S(out, "err scene 1\n");
    /* Every item as OP_JOB, in lease order (w = pass x tiles + tile). */
    mesh_pt_tile ref[TILES];
    for (uint32_t t = 0; t < TILES; t++) CHECK(mesh_pt_tile_render(scene, t, PASSES, PT_TONEMAP_ACES, &ref[t]));
    mesh_sim_set_u64(view + PT_V_JOB_BASE, 0);
    for (uint32_t w = 0; w < TILES * PASSES; w++) {
        uint32_t t = w % TILES, pass = w / TILES, r = 0;
        uint32_t tk = mesh_sim_publish(mb, 0, MESH_OP_JOB, w);
        CHECK(mesh_sim_wait(mb, 0, tk, &r, 300000));
        uint64_t dp = ref[t].part_digests[pass];
        CHECK_U(r, (uint32_t)dp & 0x7fffffffu);
        uint32_t rec = PT_V_JOB_OUT + PT_JOB_REC * (tk % MESH_PROG_RESULTS);
        CHECK_U(mesh_sim_u32(view + rec), t);
        CHECK_U(mesh_sim_u32(view + rec + 4), pass);
        CHECK_U(mesh_sim_u64(view + rec + 8), dp);
        /* The payload: the pass sums' digest over its words. */
        CHECK_U(pt_payload_digest((const uint32_t *)(const void *)(view + pt_view_payload(tk % MESH_PROG_RESULTS))), dp);
    }
    CHECK_U(mesh_sim_u32(view + PT_V_DONE), TILES);
    CHECK_U(mesh_sim_u32(view + PT_V_ITEMS), TILES * PASSES);
    check_tiles(PT_TONEMAP_ACES, PASSES);
    /* A re-run of a folded item: its pass digest again, nothing folded (record's acc digest 0). */
    uint32_t tk = mesh_sim_publish(mb, 0, MESH_OP_JOB, 5), r = 0;
    CHECK(mesh_sim_wait(mb, 0, tk, &r, 300000));
    CHECK_U(r, (uint32_t)ref[1].part_digests[1] & 0x7fffffffu);
    CHECK_U(mesh_sim_u64(view + PT_V_JOB_OUT + PT_JOB_REC * (tk % MESH_PROG_RESULTS) + 16), 0);
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_JOB, TILES * PASSES), MESH_R_JOB_FAIL);
    /* OP_TONEMAP: Reinhard from the same accumulators. */
    CHECK_U(mesh_sim_call(mb, 0, PT_OP_TONEMAP, PT_TONEMAP_REINHARD), MESH_R_NONE);
    check_tiles(PT_TONEMAP_REINHARD, PASSES);
    CHECK_U(mesh_sim_call(mb, 0, PT_OP_TONEMAP, 2), MESH_R_BAD_OP);
    /* OP_PASS: the pass sums only. */
    tk = mesh_sim_publish(mb, 0, PT_OP_PASS, 2);
    CHECK(mesh_sim_wait(mb, 0, tk, &r, 300000));
    CHECK_U(r, (uint32_t)ref[2].part_digests[0] & 0x7fffffffu);
    /* pathtrace-live: OP_TASK items from a restart, pass by pass. */
    CHECK_U(mesh_sim_call(mb, 0, PT_OP_TONEMAP, PT_TONEMAP_ACES), MESH_R_NONE);
    CHECK_U(mesh_sim_call(mb, 0, PT_OP_RESTART, 0), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view + PT_V_NEXT), 0);
    for (uint32_t w = 0; w < TILES * PASSES; w++) CHECK_U(mesh_sim_call(mb, 0, MESH_OP_TASK, w), w / TILES + 1);
    CHECK_U(mesh_sim_u32(view + PT_V_NEXT), TILES * PASSES);
    check_tiles(PT_TONEMAP_ACES, PASSES);
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_TASK, TILES * PASSES), MESH_R_NONE);
    /* OP_SET: settable words only. */
    CHECK_U(mesh_sim_call(mb, 0, PT_OP_SET, pt_set_arg(PT_H_SPP, 4)), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view + PT_V_SCENE + 4 * PT_H_SPP), 4);
    CHECK_U(mesh_sim_call(mb, 0, PT_OP_SET, pt_set_arg(PT_H_MAGIC, 4)), MESH_R_BAD_OP);
    for (uint32_t t = 0; t < TILES; t++) mesh_pt_tile_free(&ref[t]);
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, 0) + MESH_P_FAULT), 0);
    mesh_sim_halt(mb, 0, l);
    mesh_sim_mailbox_free(mb);
    return mesh_check_done();
}
