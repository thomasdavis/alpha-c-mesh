/* The wasm module built: its magic and version, and it exports mesh_abi_check. */
#include "mesh/check.h"

#include <stdlib.h>

int main(void) {
    const char *path = getenv("SEXC_ARTIFACT");
    if (!CHECK(path != NULL)) return mesh_check_done();
    FILE *f = fopen(path, "rb");
    if (!CHECK(f != NULL)) return mesh_check_done();
    static unsigned char wasm[1 << 20];
    size_t n = fread(wasm, 1, sizeof wasm, f);
    fclose(f);
    CHECK(n > 8 && memcmp(wasm, "\0asm\1\0\0\0", 8) == 0);
    int found = 0;
    for (size_t i = 0; i + 14 <= n; i++) found |= memcmp(wasm + i, "mesh_abi_check", 14) == 0;
    CHECK(found);
    return mesh_check_done();
}
