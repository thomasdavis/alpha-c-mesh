/* Widths against Rust's unicode-width 0.2.2: every scalar value's single-character width hashes
 * to what Rust computed (None as 255), and strings with the sequences it special-cases. */
#include "mesh/check.h"
#include "tui/unicode_width.h"

#include <stdint.h>
#include <string.h>

#define W(s) tui_str_width((s), sizeof(s) - 1)

int main(void) {
    uint64_t h = 0xcbf29ce484222325u;
    for (uint32_t cp = 0; cp <= 0x10FFFF; cp++) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        int w = tui_char_width(cp);
        h ^= (uint64_t)(w < 0 ? 255 : w);
        h *= 0x100000001b3u;
    }
    CHECK_U(h, 0x2f2b849f2972fccbu);
    CHECK_U(W(""), 0);
    CHECK_U(W("abc"), 3);
    CHECK_U(W("a\tb"), 3);
    CHECK_U(W("\r\n"), 1);
    CHECK_U(W("a\r"), 2);
    CHECK_U(W("\xE2\x86\x92\xE2\x86\x93\xE2\x86\x91"), 3);                          /* arrows */
    CHECK_U(W("\xC2\xB5s"), 2);                                                     /* µs */
    CHECK_U(W("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"), 6);                          /* CJK */
    CHECK_U(W("e\xCC\x81"), 1);
    CHECK_U(W("\xF0\x9F\x91\x8D"), 2);
    CHECK_U(W("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD"), 2);
    CHECK_U(W("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"), 2);
    CHECK_U(W("\xF0\x9F\x87\xA6\xF0\x9F\x87\xBA"), 2);
    CHECK_U(W("\xF0\x9F\x87\xA6\xF0\x9F\x87\xBA\xF0\x9F\x87\xB3"), 3);
    CHECK_U(W("#\xEF\xB8\x8F\xE2\x83\xA3"), 2);
    CHECK_U(W("\xE2\x98\xBA\xEF\xB8\x8F"), 2);
    CHECK_U(W("\xE2\x98\xBA\xEF\xB8\x8E"), 1);
    CHECK_U(W("\xE2\x8C\x9A\xEF\xB8\x8E"), 1);
    CHECK_U(W("\xD9\x84\xD8\xA2"), 1);                                              /* lam alef */
    CHECK_U(W("\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5\xF3\xA0\x81\xAE\xF3\xA0\x81\xA7\xF3\xA0\x81\xBF"), 2);
    CHECK_U(W("x\xE2\x80\x8By"), 2);
    CHECK_U(W("\xC2\xAD"), 0);
    CHECK_U(W("\xE1\x84\x80\xE1\x85\xA1"), 2);
    CHECK_U(W("\xEF\xBD\xB1\xEF\xBE\x9E"), 1);
    CHECK_U(W("\x7F"), 1);
    CHECK_U(W("\xC2\xA0"), 1);
    CHECK_U(W("a\xE2\x80\x8D" "b"), 2);
    CHECK_U(W("\xE1\x9F\x92\xE1\x9E\x80"), 0);
    CHECK_U(W("\xE2\xA3\xBF\xE2\xA1\x80"), 2);
    CHECK_U(W("\xE2\x96\x88\xE2\x96\x80\xE2\x96\x84"), 3);
    CHECK_U(W("\xE2\x94\x81\xE2\x94\x82\xE2\x95\xAD\xE2\x95\xAE\xE2\x95\xB0\xE2\x95\xAF"), 6);
    CHECK_U(W("\xE2\x80\xA6\xC2\xB7\xE2\x80\xA2"), 3);
    CHECK_U(W("\xC2\xB0" "C\xC3\x97"), 3);
    CHECK_U(W("\xEF\xBF\xBD"), 1);
    CHECK_I(tui_char_width('\t'), -1);
    CHECK_I(tui_char_width(0x85), -1);
    CHECK_I(tui_char_width(0x4E00), 2);
    CHECK_U(tui_cstr_width("p50 \xC2\xB5s"), 6);
    return mesh_check_done();
}
