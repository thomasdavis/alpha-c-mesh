/* fleet.rs group_epochs. */
#include "mesh/check.h"
#include "mesh/fleet_groups.h"
#include "mesh/mt.h"

#include <stdlib.h>


int main(void) {
    mfd_group g = {.name = mt_strdup("train"), .program = mt_strdup("train"), .epoch = 1, .cycle = 3, .op = {MFD_OP_UPGRADE, 0, 0},
                   .members = ml_of((uint32_t[]){0, 1, 2}, 3), .state = MFD_RUNNING, .on_loss = MFD_LOSS_PAUSE};
    mfd_group p, r, q, s, t, x;
    CHECK(!mfg_policy(&g, 7, -1, &x));
    CHECK(mfg_policy(&g, 3, -1, &p));
    CHECK(p.state == MFD_PAUSED && p.epoch == 1 && p.members.n == 3);
    CHECK(!mfg_policy(&p, 3, 60000, &x));
    CHECK(mfg_policy(&p, 15, -1, &r));
    CHECK(r.state == MFD_RUNNING && r.epoch == 2 && r.cycle == 0 && r.members.n == 3 && r.op.kind == MFD_OP_NONE);
    q = mfd_group_clone(&p);
    q.on_loss = MFD_LOSS_REFORM;
    q.auto_ms = 2000;
    CHECK(!mfg_policy(&q, 5, 1999, &x));
    CHECK(mfg_policy(&q, 5, 2000, &s));
    CHECK(s.epoch == 2 && s.members.n == 2 && s.members.v[0] == 0 && s.members.v[1] == 2 && s.state == MFD_RUNNING);
    /* A member that comes back later does not disturb the running epoch... */
    CHECK(!mfg_policy(&s, 7, -1, &x));
    /* ... until the next reform, which takes every eligible up member. */
    ml_list all = ml_of((uint32_t[]){0, 1, 2}, 3), empty = {0};
    char *err = NULL;
    CHECK(mfg_reform(&s, &all, &t, &err) && t.epoch == 3 && t.members.n == 3);
    CHECK(!mfg_reform(&s, &empty, &x, &err));
    CHECK_S(err, "group train: no member that can run train is up");
    free(err);
    /* Survivors gone too: no reform. */
    CHECK(!mfg_policy(&q, 8, 5000, &x));
    ml_free(&all);
    mfd_group_free(&g), mfd_group_free(&p), mfd_group_free(&r), mfd_group_free(&q), mfd_group_free(&s), mfd_group_free(&t);
    return mesh_check_done();
}
