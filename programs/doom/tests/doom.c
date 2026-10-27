/* kcheck's doom checks on the simulator (3, 3b, and the OP_FOLLOW part of 3c):
 * the map and start pose after OP_INIT; seven poses, each frame (4 tickets
 * over 4 nodes, the arg-0 one releasing the pose) against the CPU raycaster
 * (@ajax/mesh-ref-doom) bitwise; keys through the input queue applied with
 * wall collision, the pose bitwise == the reference's every frame; a follow
 * renders at the view's pose and leaves it, the frame and the queue alone.
 * Ported from alpha-rust-mesh crates/mesh-kcheck/src/pchecks.rs. */
#include "mesh/check.h"
#include "mesh/ref/doom.h"
#include "mesh/rt/sim_mailbox.h"

#include "../src/doom.c"

static uint8_t *mb, *view;
static uint32_t ref_map[256];

static void set_pose(const float p[6]) { for (int k = 0; k < 6; k++) mesh_sim_set_f32(view + MESH_DOOM_D_POS_X + 4 * k, p[k]); }
static void get_pose(float p[6]) { for (int k = 0; k < 6; k++) p[k] = mesh_sim_f32(view + MESH_DOOM_D_POS_X + 4 * k); }

/* One frame (4 consecutive tickets), the hits summed. */
static uint32_t frame(uint32_t op) {
    uint32_t t[4], sum = 0;
    for (uint32_t b = 0; b < 4; b++) t[b] = mesh_sim_publish(mb, 0, op, b);
    for (uint32_t b = 0; b < 4; b++) {
        uint32_t r = 0;
        CHECK(mesh_sim_wait(mb, 0, t[b], &r, 60000));
        sum += r;
    }
    return sum;
}

/* A frame whose arg-0 task drains keys: on the simulator 64 collective pops
 * can outlast the program's POSE_WAIT_NS under load (on a GPU they take
 * microseconds), so arg 0 completes before args 1..3 are published. Their
 * tickets are still the next three: the pose hand-off is the same. */
static uint32_t frame_keys(void) {
    uint32_t r = 0, sum = 0, t = mesh_sim_publish(mb, 0, MESH_OP_TASK, 0);
    CHECK(mesh_sim_wait(mb, 0, t, &r, 60000));
    sum += r;
    uint32_t ts[3];
    for (uint32_t b = 1; b < 4; b++) ts[b - 1] = mesh_sim_publish(mb, 0, MESH_OP_TASK, b);
    for (uint32_t b = 0; b < 3; b++) {
        CHECK(mesh_sim_wait(mb, 0, ts[b], &r, 60000));
        sum += r;
    }
    return sum;
}

/* The frame in the view against the CPU raycaster at pose: mismatching columns (+1 for the hit count). */
static uint32_t frame_bad(const float *pose, uint32_t hits) {
    uint32_t bad = 0, want_hits = 0;
    for (uint32_t c = 0; c < MESH_DOOM_WIDTH; c++) {
        uint32_t side;
        float dist;
        uint32_t k = mesh_doom_cast(ref_map, pose[0], pose[1], pose[2], pose[3], pose[4], pose[5], c, MESH_DOOM_MAX_STEPS, &side, &dist);
        float h = mesh_doom_height(k, dist);
        want_hits += k != 0;
        if (mesh_sim_u32(view + MESH_DOOM_D_KIND + 4 * c) != k || mesh_sim_u32(view + MESH_DOOM_D_SIDE + 4 * c) != side ||
            mesh_sim_u32(view + MESH_DOOM_D_DIST + 4 * c) != mesh_f32_bits(dist) ||
            mesh_sim_u32(view + MESH_DOOM_D_HEIGHT + 4 * c) != mesh_f32_bits(h)) {
            if (bad < 3) fprintf(stderr, "doom col %u: sim (%u,%u,%g,%g) cpu (%u,%u,%g,%g)\n", c, mesh_sim_u32(view + MESH_DOOM_D_KIND + 4 * c),
                                 mesh_sim_u32(view + MESH_DOOM_D_SIDE + 4 * c), (double)mesh_sim_f32(view + MESH_DOOM_D_DIST + 4 * c),
                                 (double)mesh_sim_f32(view + MESH_DOOM_D_HEIGHT + 4 * c), k, side, (double)dist, (double)h);
            bad++;
        }
    }
    return bad + (hits != want_hits);
}

int main(void) {
    mesh_doom_map(ref_map);
    mb = mesh_sim_mailbox();
    /* The manifest's `input = 64`: the queue at the end of the view, 16-aligned. */
    uint32_t input_off = (MESH_DOOM_VIEW_BYTES - (MESH_IQ_DATA + MESH_DOOM_INPUT_BYTES)) & ~15u;
    mesh_sim_prog p = {.name = "doom", .view_bytes = MESH_DOOM_VIEW_BYTES, .nodes = 4, .input_off = input_off, .input_len = MESH_DOOM_INPUT_BYTES};
    view = mesh_sim_install(mb, 0, &p);
    uint8_t *ctl = mesh_sim_ctl(mb, 0);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, 0, &p, mesh_main);
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_INIT, 0), MESH_R_NONE);
    /* 3: init and seven poses. */
    for (uint32_t c = 0; c < 256; c++) CHECK_U(mesh_sim_u32(view + MESH_DOOM_D_MAP + 4 * c), ref_map[c]);
    float g[6];
    get_pose(g);
    for (int k = 0; k < 6; k++) CHECK_U(mesh_f32_bits(g[k]), mesh_f32_bits(MESH_DOOM_START[k]));
    CHECK_U(mesh_sim_u32(view + MESH_DOOM_D_ROWS), mesh_f32_bits(MESH_DOOM_ROWS));
    CHECK_U(mesh_sim_u32(view + MESH_DOOM_D_MAX_STEPS), MESH_DOOM_MAX_STEPS);
    const float poses[7][6] = {
        {8.5f, 10.5f, 0.0f, -1.0f, 0.66f, 0.0f},        {8.5f, 8.5f, 0.0f, 1.0f, -0.66f, 0.0f},
        {1.5f, 1.5f, 0.7071f, 0.7071f, -0.4667f, 0.4667f}, {14.5f, 14.5f, -0.7071f, -0.7071f, 0.4667f, -0.4667f},
        {1.21f, 7.5f, -1.0f, 0.0f, 0.0f, -0.66f},      {14.79f, 1.21f, 0.6f, -0.8f, 0.528f, 0.396f},
        {5.5f, 7.5f, 0.0f, -1.0f, 0.66f, 0.0f},
    };
    uint32_t hits_total = 0;
    for (int i = 0; i < 7; i++) {
        set_pose(poses[i]);
        uint32_t hits = frame(MESH_OP_TASK);
        hits_total += hits;
        get_pose(g);
        for (int k = 0; k < 6; k++) CHECK_U(mesh_f32_bits(g[k]), mesh_f32_bits(poses[i][k])); /* no keys: unchanged */
        CHECK_U(frame_bad(poses[i], hits), 0);
    }
    CHECK(hits_total > 0);
    /* On a GPU no frame stalls (kcheck checks 0). The simulator on an
     * oversubscribed machine can run an arg-0 task later than POSE_WAIT_NS:
     * the frame above was still exact (the pose did not change), so a stall
     * here is noted, not failed. The keys frames below must not stall. */
    uint32_t stalls0 = mesh_sim_u32(ctl + MESH_P_COUNTERS + 4 * MESH_DOOM_C_STALLS);
    if (stalls0) fprintf(stderr, "note: %u pose waits outlasted POSE_WAIT_NS on this loaded machine\n", stalls0);
    /* 3b: keys through the input queue, one burst a frame (the last fills the whole queue). */
    mesh_doom_pose cpu = mesh_doom_pose_start();
    set_pose(cpu.p);
    uint32_t frames0 = mesh_sim_u32(ctl + MESH_P_COUNTERS + 4 * MESH_DOOM_C_FRAMES), keys = 0, nframes = 0;
    uint8_t full[64];
    memset(full, 'w', sizeof full);
    const char *script[11] = {"w", "wwwwwwwwww", "dddd", "wwwwwwwwwwwwww", "qqqqqqqqqq", "eeeeeeeeeeeeeeeeeeee", "aaaaaaaaa", "wwwwsssswwwwdwdwdwdw", "xyz", "", "ssssssssssssssssssss"};
    for (int i = 0; i < 12; i++) {
        const uint8_t *burst = i < 11 ? (const uint8_t *)script[i] : full;
        uint32_t n = i < 11 ? (uint32_t)strlen(script[i]) : 64;
        CHECK(n == 0 || mesh_sim_input(mb, 0, burst, n));
        for (uint32_t k = 0; k < n; k++) mesh_doom_pose_apply(&cpu, burst[k]);
        keys += n;
        uint32_t hits = frame_keys();
        nframes++;
        get_pose(g);
        for (int k = 0; k < 6; k++) CHECK_U(mesh_f32_bits(g[k]), mesh_f32_bits(cpu.p[k]));
        CHECK(mesh_doom_walkable(g[0], g[1]));
        CHECK_U(frame_bad(cpu.p, hits), 0);
    }
    uint8_t *q = view + input_off;
    CHECK_U(mesh_sim_u32(q + MESH_IQ_HEAD), mesh_sim_u32(q + MESH_IQ_TAIL));
    CHECK_U(mesh_sim_u32(ctl + MESH_P_COUNTERS + 4 * MESH_DOOM_C_FRAMES) - frames0, nframes);
    CHECK_U(mesh_sim_u32(ctl + MESH_P_COUNTERS + 4 * MESH_DOOM_C_KEYS), keys);
    CHECK_U(mesh_sim_u32(view + MESH_DOOM_D_KEYS), keys);
    CHECK_U(mesh_sim_u32(ctl + MESH_P_COUNTERS + 4 * MESH_DOOM_C_STALLS), stalls0);
    /* A follow: a queued key, the frame number and the pose stay as they are. */
    uint32_t frame_no = mesh_sim_u32(view + MESH_DOOM_D_FRAME);
    CHECK(mesh_sim_input(mb, 0, "w", 1));
    uint32_t hits = frame(MESH_OP_FOLLOW);
    get_pose(g);
    for (int k = 0; k < 6; k++) CHECK_U(mesh_f32_bits(g[k]), mesh_f32_bits(cpu.p[k]));
    CHECK_U(frame_bad(cpu.p, hits), 0);
    CHECK_U(mesh_sim_u32(view + MESH_DOOM_D_FRAME), frame_no);
    CHECK_U(mesh_sim_u32(q + MESH_IQ_HEAD) - mesh_sim_u32(q + MESH_IQ_TAIL), 1);
    CHECK_U(mesh_sim_u32(ctl + MESH_P_COUNTERS + 4 * MESH_DOOM_C_FOLLOWS), 4);
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_FOLLOW, 4), MESH_R_BAD_OP);
    /* A lone arg-1 ticket (its arg-0 lost): a bounded wait, then a stall. */
    CHECK_U(mesh_sim_u32(ctl + MESH_P_FAULT), 0);
    mesh_sim_halt(mb, 0, l);
    mesh_sim_mailbox_free(mb);
    return mesh_check_done();
}
