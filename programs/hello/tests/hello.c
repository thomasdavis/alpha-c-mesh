/* hello on the CPU simulator: init sets the magic, tasks compute arg * 3 + 7
 * (wrapping), counters count, and a halted program relaunches where it was. */
#include "mesh/check.h"
#include "mesh/rt/sim_mailbox.h"

#include "../src/hello.c"

int main(void) {
    uint8_t *mb = mesh_sim_mailbox();
    mesh_sim_prog p = {.name = "hello", .view_bytes = 0x1000, .nodes = 2};
    uint8_t *view = mesh_sim_install(mb, 3, &p);
    uint8_t *ctl = mesh_sim_ctl(mb, 3);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, 3, &p, mesh_main);
    CHECK_U(mesh_sim_call(mb, 3, MESH_OP_INIT, 0), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view), HELLO_MAGIC);
    CHECK_U(mesh_sim_call(mb, 3, MESH_OP_TASK, 5), 22);
    CHECK_U(mesh_sim_u32(view + 4), 5);
    CHECK_U(mesh_sim_u32(view + 8), 22);
    CHECK_U(mesh_sim_call(mb, 3, MESH_OP_TASK, 0xffffffffu), 4); /* wraps */
    CHECK_U(mesh_sim_call(mb, 3, 9, 0), MESH_R_BAD_OP);
    /* A batch of 40 tickets over both nodes. */
    uint32_t t[40];
    for (uint32_t i = 0; i < 40; i++) t[i] = mesh_sim_publish(mb, 3, MESH_OP_TASK, i);
    for (uint32_t i = 0; i < 40; i++) {
        uint32_t r = 0;
        CHECK(mesh_sim_wait(mb, 3, t[i], &r, 10000));
        CHECK_U(r, i * 3 + 7);
    }
    CHECK_U(mesh_sim_u32(ctl + MESH_P_COUNTERS), 42);     /* tasks: the 2 above and the 40 (not the bad op) */
    CHECK_U(mesh_sim_u32(ctl + MESH_P_COUNTERS + 4), 1); /* inits */
    CHECK_U(mesh_sim_u32(ctl + MESH_P_ONLINE), 2);
    CHECK_U(mesh_sim_u32(ctl + MESH_P_DONE), 44);
    mesh_sim_halt(mb, 3, l);
    /* Relaunched: the next ticket runs (the ring was rewound to the tail). */
    l = mesh_sim_launch_prog(mb, 3, &p, mesh_main);
    CHECK_U(mesh_sim_call(mb, 3, MESH_OP_TASK, 1), 10);
    CHECK_U(mesh_sim_u32(ctl + MESH_P_FAULT), 0);
    mesh_sim_halt(mb, 3, l);
    mesh_sim_mailbox_free(mb);
    return mesh_check_done();
}
