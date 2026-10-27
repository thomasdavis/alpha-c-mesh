/* fleet.rs document_round_trips (the Persist part). */
#include "mesh/check.h"
#include "mesh/fleet_persist.h"

#include <stdlib.h>

int main(void) {
    mfp_persist p = {.term = 5, .has_voted = true, .voted = 2};
    p.accepted.version = 7;
    p.accepted.term = 3;
    msn_needs n = msn_default();
    mfd_prog_put(&p.accepted, "hello", mfd_prog_new("hello", 0xabc, true, &n));
    msn_free(&n);
    mt_strs w = {0};
    mt_push(&w, "split");
    mfd_place_put(&p.accepted, "doom", w);
    p.committed = mfd_doc_clone(&p.accepted);
    p.committed.version = 6;
    char *e = mfp_encode(&p, 4);
    mfp_persist back;
    char *err = NULL;
    CHECK(mfp_decode(e, 4, &back, &err));
    CHECK(mfp_eq(&back, &p));
    mfp_free(&back);
    CHECK(!mfp_decode(e, 3, &back, &err));
    CHECK(mt_contains(err, "cluster of 4"));
    free(err);
    free(e);
    CHECK(!mfp_decode("term 1\n", 4, &back, &err));
    CHECK_S(err, "incomplete (no accepted and committed documents)");
    free(err);
    CHECK(!mfp_decode("junk\n", 4, &back, &err));
    CHECK_S(err, "bad line \"junk\"");
    free(err);
    CHECK(!mfp_decode("systems 4\naccepted 999\ndoc", 4, &back, &err));
    CHECK_S(err, "accepted: truncated");
    free(err);
    /* A fresh member's state: no vote, empty documents. */
    mfp_persist z = {0};
    e = mfp_encode(&z, 3);
    CHECK(mfp_decode(e, 3, &back, &err) && mfp_eq(&back, &z));
    mfp_free(&back);
    free(e);
    mfp_free(&p);
    return mesh_check_done();
}
