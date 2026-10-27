/* assemble.rs's bmp check (from default_reference_digests). */
#include "mesh/assemble_bmp.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    size_t n;
    uint8_t *b = masm_bmp(2, 1, (const uint8_t[]){1, 2, 3, 4, 5, 6}, &n);
    CHECK_U(n, 62);
    CHECK(memcmp(b + 54, (const uint8_t[]){3, 2, 1, 6, 5, 4, 0, 0}, 8) == 0);
    CHECK(b[0] == 'B' && b[1] == 'M' && b[2] == 62 && b[10] == 54 && b[14] == 40 && b[18] == 2 && b[22] == 1 && b[26] == 1 && b[28] == 24);
    free(b);
    /* Two rows: bottom-up. */
    b = masm_bmp(1, 2, (const uint8_t[]){1, 2, 3, 4, 5, 6}, &n);
    CHECK(n == 62 && memcmp(b + 54, (const uint8_t[]){6, 5, 4, 0, 3, 2, 1, 0}, 8) == 0);
    free(b);
    return mesh_check_done();
}
