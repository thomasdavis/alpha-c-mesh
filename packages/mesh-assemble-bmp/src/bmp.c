/* assemble.rs's bmp. */
#include "mesh/assemble_bmp.h"
#include "mesh/strbuf.h"

#include <string.h>

static void le32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> 8 * i);
}

uint8_t *masm_bmp(uint32_t w, uint32_t h, const uint8_t *raster, size_t *len) {
    uint32_t row = (3 * w + 3) / 4 * 4, size = 54 + row * h;
    uint8_t *b = mxalloc(size);
    b[0] = 'B';
    b[1] = 'M';
    le32(b + 2, size);
    le32(b + 6, 0);
    le32(b + 10, 54);
    le32(b + 14, 40);
    le32(b + 18, w);
    le32(b + 22, h);
    b[26] = 1;
    b[28] = 24;
    uint32_t rest[] = {0, row * h, 2835, 2835, 0, 0};
    for (int i = 0; i < 6; i++) le32(b + 30 + 4 * i, rest[i]);
    uint8_t *p = b + 54;
    for (uint32_t y = h; y-- > 0;) {
        const uint8_t *r = raster + (size_t)3 * w * y;
        for (uint32_t x = 0; x < w; x++) {
            p[3 * x] = r[3 * x + 2];
            p[3 * x + 1] = r[3 * x + 1];
            p[3 * x + 2] = r[3 * x];
        }
        p += row; /* padding stays zero */
    }
    *len = size;
    return b;
}
