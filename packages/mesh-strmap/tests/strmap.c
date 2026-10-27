/* Sorted, replace or keep, delete. */
#include "mesh/check.h"
#include "mesh/strmap.h"

int main(void) {
    ms_map m = {0};
    CHECK(ms_set(&m, "systems", "3", false));
    CHECK(ms_set(&m, "1.peer", "a:1", false));
    CHECK(ms_set(&m, "0.peer", "b:2", false));
    CHECK(!ms_set(&m, "systems", "4", false));
    CHECK_S(ms_get(&m, "systems"), "3");
    CHECK(ms_set(&m, "systems", "4", true));
    CHECK_S(ms_get(&m, "systems"), "4");
    CHECK_S(m.k[0], "0.peer");
    CHECK_S(m.k[2], "systems");
    CHECK(ms_get(&m, "zz") == NULL);
    CHECK(ms_del(&m, "1.peer") && !ms_del(&m, "1.peer") && m.n == 2);
    for (int i = 0; i < 40; i++) {
        char k[8];
        snprintf(k, sizeof k, "k%02d", 39 - i);
        ms_set(&m, k, "v", false);
    }
    for (size_t i = 1; i < m.n; i++) CHECK(strcmp(m.k[i - 1], m.k[i]) < 0);
    ms_free(&m);
    return mesh_check_done();
}
