/* jobs.rs's lid checks (from `a_restarted_coordinator_with_its_clock_behind`). */
#include "mesh/jobs/lid.h"
#include "mesh/check.h"

int main(void) {
    uint64_t x;
    CHECK(!mj_lid_next(3, (uint64_t)4 << 40, &x));
    CHECK(mj_lid_next(4, (uint64_t)4 << 40 | 7, &x) && x == ((uint64_t)4 << 40 | 8));
    CHECK(mj_lid_next(5, (uint64_t)4 << 40 | 7, &x) && x == (uint64_t)5 << 40);
    CHECK(mj_lid_next(1, 0, &x) && x == (uint64_t)1 << 40);
    CHECK(!mj_lid_next((uint64_t)1 << 24, 0, &x));
    CHECK(mj_lease_base((uint64_t)5 << 40) > mj_lease_base((uint64_t)4 << 40 | 7));
    CHECK(mj_lease_base((uint64_t)4 << 40 | 8) > mj_lease_base((uint64_t)4 << 40 | 7));
    CHECK_U(mj_lease_base((uint64_t)4 << 40 | 7), (uint64_t)4 << 40 | (uint64_t)7 << 32);
    CHECK_U(mj_lid_term((uint64_t)9 << 40 | 3), 9);
    return mesh_check_done();
}
