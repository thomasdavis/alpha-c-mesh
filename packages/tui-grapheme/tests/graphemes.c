/* Grapheme boundaries against Rust's unicode-segmentation 1.13.3 (graphemes(true)): hand-picked
 * sequences, and a 7.5 kB mixed text (every 7th code point below U+3000, emoji, Hangul, flags,
 * Indic conjuncts, combining accents and ZWJs sprinkled in) whose cluster lengths hash to what
 * Rust computed. */
#include "mesh/check.h"
#include "tui/grapheme.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t count(const char *s) {
    size_t n = 0, len = strlen(s);
    for (size_t i = 0; i < len; i = tui_grapheme_next(s, len, i)) n++;
    return n;
}

int main(void) {
    CHECK_U(count(""), 0);
    CHECK_U(count("abc"), 3);
    CHECK_U(count("\r\n"), 1);
    CHECK_U(count("e\xCC\x81"), 1);                                             /* e + U+0301 */
    CHECK_U(count("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD"), 1);                     /* thumbs up + skin tone */
    CHECK_U(count("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"), 1); /* family */
    CHECK_U(count("\xF0\x9F\x87\xA6\xF0\x9F\x87\xBA\xF0\x9F\x87\xB3"), 2);     /* AU + N */
    CHECK_U(count("#\xEF\xB8\x8F\xE2\x83\xA3"), 1);                             /* keycap */
    CHECK_U(count("\xE1\x84\x80\xE1\x85\xA1"), 1);                             /* Hangul L V */
    CHECK_U(count("a\xE2\x80\x8D" "b"), 2);                                     /* a ZWJ | b */
    CHECK_U(count("\xEF\xBD\xB1\xEF\xBE\x9E"), 1);                             /* halfwidth ka + voiced mark */
    CHECK_U(count("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7"), 1);                 /* ka virama ssa (GB9c) */

    FILE *f = fopen("tests/data/mixed.txt", "rb");
    CHECK(f != NULL);
    if (f) {
        char *text = malloc(16384);
        size_t len = fread(text, 1, 16384, f);
        fclose(f);
        CHECK_U(len, 7582);
        uint64_t h = 0xcbf29ce484222325u, n = 0;
        for (size_t i = 0; i < len;) {
            size_t end = tui_grapheme_next(text, len, i);
            uint32_t g = (uint32_t)(end - i);
            for (int k = 0; k < 4; k++) {
                h ^= (g >> (8 * k)) & 0xff;
                h *= 0x100000001b3u;
            }
            n++;
            i = end;
        }
        CHECK_U(n, 1768);
        CHECK_U(h, 0xf7e2321a1b4685f5u);
        free(text);
    }
    return mesh_check_done();
}
