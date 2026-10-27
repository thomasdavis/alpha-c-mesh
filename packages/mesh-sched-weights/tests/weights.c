/* Weights::from_keys: defaults, overrides, the floor on tick_ms, errors in the Rust's words. */
#include "mesh/check.h"
#include "mesh/sched_weights.h"

#include <stdlib.h>

int main(void) {
    ms_map m = {0};
    msw_weights w;
    char *err = NULL;
    CHECK(msw_from_keys(&m, &w, &err));
    msw_weights d = msw_default();
    CHECK(msw_eq(&w, &d));
    ms_set(&m, "sched_hold_ms", "2000", true);
    ms_set(&m, "sched_improve", "0.5", true);
    ms_set(&m, "sched_tick_ms", "10", true);
    ms_set(&m, "sched_owner_share", "0.4", true);
    CHECK(msw_from_keys(&m, &w, &err));
    CHECK(w.hold_ms == 2000 && w.improve == 0.5 && w.tick_ms == 100 && w.owner_share == 0.4f && w.dwell_ms == 60000);
    ms_set(&m, "sched_hold_ms", "x", true);
    CHECK(!msw_from_keys(&m, &w, &err));
    CHECK_S(err, "cluster: sched_hold_ms = \"x\": an integer");
    free(err);
    ms_set(&m, "sched_hold_ms", "1", true);
    ms_set(&m, "sched_reserve", "1", true);
    CHECK(!msw_from_keys(&m, &w, &err));
    CHECK_S(err, "cluster: sched_improve 0..10, sched_reserve 0..1");
    free(err);
    ms_set(&m, "sched_reserve", "nope", true);
    CHECK(!msw_from_keys(&m, &w, &err));
    CHECK_S(err, "cluster: sched_reserve = \"nope\": a number");
    free(err);
    ms_free(&m);
    return mesh_check_done();
}
