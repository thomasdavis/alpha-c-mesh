/* The PTX module built: it targets an sm_ GPU and holds the kernel entry. */
#include "mesh/check.h"

#include <stdlib.h>

int main(void) {
    const char *path = getenv("SEXC_ARTIFACT");
    if (!CHECK(path != NULL)) return mesh_check_done();
    FILE *f = fopen(path, "rb");
    if (!CHECK(f != NULL)) return mesh_check_done();
    static char ptx[1 << 22];
    size_t n = fread(ptx, 1, sizeof ptx - 1, f);
    fclose(f);
    ptx[n] = 0;
    CHECK(strstr(ptx, ".target sm_") != NULL);
    CHECK(strstr(ptx, ".entry mesh_abi_check") != NULL);
    return mesh_check_done();
}
