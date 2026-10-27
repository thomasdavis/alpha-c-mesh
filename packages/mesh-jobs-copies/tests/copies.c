/* jobs.rs's `copies` test. */
#include "mesh/jobs/copies.h"
#include "mesh/check.h"

static uint32_t c(uint32_t me, uint32_t n, uint32_t leader, uint32_t alive, uint32_t refused) {
    uint32_t w = 0xdead;
    CHECK(mj_copies_for(me, n, true, leader, alive, refused, &w));
    return w;
}

int main(void) {
    /* 4 members, leader 0: member 1's payload at 1, 0 and 2; the leader's at 0, 1, 2; with 2 down, 3 instead. */
    CHECK_U(c(1, 4, 0, 0xf, 0), 0x7);
    CHECK_U(c(0, 4, 0, 0xf, 0), 0x7);
    CHECK_U(c(1, 4, 0, 0xb, 0), 0xb);
    CHECK_U(c(3, 4, 0, 0xf, 0), 0xb);
    /* A refusal (a full store) is skipped, but never the leader's; no leader: none. */
    CHECK_U(c(1, 4, 0, 0xf, 0x6), 0x9);
    CHECK_U(c(1, 4, 0, 0xf, 0x1), 0x7);
    CHECK_U(c(0, 1, 0, 0x1, 0), 0x1);
    uint32_t w;
    CHECK(!mj_copies_for(1, 4, false, 0, 0xf, 0, &w));
    return mesh_check_done();
}
