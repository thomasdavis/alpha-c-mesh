/* load.rs's gpu_share_maths. */
#include "mesh/check.h"
#include "mesh/load_share.h"

#include <math.h>

int main(void) {
    mesh_nl d = {2000000000ull, 1044000000ull, 458, 0};
    mesh_share s = mesh_share_of(&d);
    CHECK(s.has_share && fabsf(s.share - 0.478f) < 1e-3f);
    CHECK(fabsf(s.gap_us - 2279.5f) < 1.0f);
    CHECK(fabsf(s.slice_us - 2087.3f) < 1.0f);
    mesh_nl zero = {0, 0, 0, 0};
    CHECK(!mesh_share_of(&zero).has_share);
    mesh_nl lone = {1000000000ull, 0, 0, 0};
    s = mesh_share_of(&lone);
    CHECK(s.share == 1.0f && s.slice_us == 0 && s.gap_us == 0);
    mesh_nl a = {100, 40, 2, 7}, b = {300, 90, 5, 10};
    mesh_nl x = mesh_nl_delta(&b, &a);
    CHECK(x.obs == 200 && x.lost == 50 && x.gaps == 3 && x.queue_us == 3);
    x = mesh_nl_delta(&a, &b);
    CHECK(x.obs == 0 && x.lost == 0 && x.gaps == 0 && x.queue_us == 0);
    mesh_nl sum = {0, 0, 0, 0}, big = {10, 50, 1, 0};
    mesh_nl_add(&sum, &big);
    CHECK_U(sum.lost, 10);
    mesh_nl m = {4360000, 1420000, 10, 900};
    s = mesh_share_of(&m);
    CHECK(fabsf(s.share - 0.6743f) < 1e-3f);
    CHECK(s.queue_us == 90.0f);
    const uint32_t q[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    CHECK_U(mesh_quantile(q, 10, 0.5), 6);
    CHECK_U(mesh_quantile(q, 10, 0.99), 10);
    CHECK_U(mesh_quantile(q, 0, 0.5), 0);
    return mesh_check_done();
}
