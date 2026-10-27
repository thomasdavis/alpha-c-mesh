/* peer.rs store_persists. */
#include "mesh/check.h"
#include "mesh/fleet_store.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <unistd.h>

int main(void) {
    char *p = mt_fmt("/tmp/meshd-fleet-%d.state", (int)getpid());
    unlink(p);
    mfs_store s;
    mfp_persist first, back;
    char *err = NULL;
    CHECK(mfs_open(p, 3, &s, &first, &err));
    mfp_persist zero = {0};
    CHECK(mfp_eq(&first, &zero));
    mfp_persist q = mfp_clone(&first);
    q.term = 9;
    q.has_voted = true;
    q.voted = 1;
    q.committed.version = 4;
    q.accepted.version = 5;
    CHECK(mfs_write(&s, &q, &err));
    mfs_close(&s);
    CHECK(mfs_open(p, 3, &s, &back, &err) && mfp_eq(&back, &q));
    mfs_close(&s);
    mfp_free(&back);
    CHECK(!mfs_open(p, 4, &s, &back, &err));
    CHECK(mt_contains(err, "cluster of 3"));
    free(err);
    CHECK(!mfs_open("/nonexistent-dir/x.state", 3, &s, &back, &err));
    free(err);
    unlink(p);
    free(p);
    mfp_free(&q);
    mfp_free(&first);
    return mesh_check_done();
}
