/* backend.rs's co_residency test, one for one. */
#include "mesh/check.h"
#include "mesh/occupancy.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <string.h>

static mesh_occ_kernel small[] = {{1, 16}, {1, 16}, {4, 16}, {1, 12}, {2, 16}, {4, 16}};

static char *with(const mesh_occ_kernel *extra, size_t ne, uint32_t sms, uint32_t bps) {
    mesh_occ_kernel k[16];
    memcpy(k, small, sizeof small);
    memcpy(k + 6, extra, ne * sizeof *extra);
    char *err = NULL;
    return mesh_co_resident(k, 6 + ne, sms, bps, &err) ? NULL : err;
}

static bool err_has(char *e, const char *needle) {
    bool ok = e && strstr(e, needle);
    if (!ok) fprintf(stderr, "error: %s\n", e ? e : "(none)");
    free(e);
    return ok;
}

int main(void) {
    /* The RTX 3060 (28 SMs, 16 blocks/SM): kcheck's set and 512-thread x 128-register nodes. */
    CHECK_U(mesh_occ_room(small, 6, 1, 32, 28, 16), 15);
    CHECK(with((mesh_occ_kernel[]){{15, 1}}, 1, 28, 16) == NULL);
    CHECK(err_has(with((mesh_occ_kernel[]){{16, 1}}, 1, 28, 16), "16 node(s) that each need a whole SM"));
    /* A GB10 (48 SMs, 24/SM): 32 whole-SM pathtrace nodes beside them. */
    CHECK_U(mesh_occ_room(small, 6, 1, 32, 48, 24), 32);
    /* Shares: two kernels of half the device each fit, a third does not. */
    CHECK(mesh_co_resident((mesh_occ_kernel[]){{24, 1}, {24, 1}}, 2, 48, 24, NULL));
    CHECK(!mesh_co_resident((mesh_occ_kernel[]){{24, 1}, {24, 1}, {1, 24}}, 3, 48, 24, NULL));
    CHECK(mesh_co_resident((mesh_occ_kernel[]){{48 * 6, 12}, {48 * 6, 12}}, 2, 48, 24, NULL));
    char *e = NULL;
    CHECK(!mesh_co_resident((mesh_occ_kernel[]){{48 * 6, 12}, {48 * 6 + 1, 12}}, 2, 48, 24, &e));
    CHECK(err_has(e, "100%"));
    /* Per kernel and the block limit. */
    e = NULL;
    CHECK(!mesh_co_resident((mesh_occ_kernel[]){{33, 1}}, 1, 32, 24, &e));
    CHECK(err_has(e, "allows 1 per SM"));
    CHECK(!mesh_co_resident((mesh_occ_kernel[]){{0, 0}}, 1, 32, 24, NULL));
    e = NULL;
    CHECK(!mesh_co_resident((mesh_occ_kernel[]){{17, 32}, {16, 32}}, 2, 1, 32, &e));
    CHECK(err_has(e, "resident blocks"));
    /* The exact messages. */
    e = NULL;
    mesh_co_resident((mesh_occ_kernel[]){{48 * 6, 12}, {48 * 6 + 1, 12}}, 2, 48, 24, &e);
    CHECK_S(e, "the loaded kernels would need 100% of the SMs' resident capacity");
    free(e);
    e = NULL;
    mesh_co_resident((mesh_occ_kernel[]){{33, 1}}, 1, 32, 24, &e);
    CHECK_S(e, "33 blocks; its occupancy allows 1 per SM x 32 SMs");
    free(e);
    return mesh_check_done();
}
