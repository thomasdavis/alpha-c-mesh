/* Decoding and repair against Rust's String::from_utf8_lossy (the strings below are what Rust
 * printed for the same bytes, as code points). */
#include "mesh/check.h"
#include "tui/utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *cps(const char *s, size_t len) {
    static char buf[512];
    size_t n = 0;
    buf[0] = 0;
    for (size_t i = 0; i < len;) {
        uint32_t cp = tui_utf8_next(s, len, &i);
        n += (size_t)snprintf(buf + n, sizeof buf - n, n ? " %x" : "%x", cp);
    }
    return buf;
}

int main(void) {
    struct {
        const char *in;
        size_t len;
        const char *want;
    } cases[] = {
        {"abc", 3, "61 62 63"},
        {"\xC3\xA9", 2, "e9"},
        {"\xE2\x86\x92", 3, "2192"},
        {"\xF0\x9F\x91\x8D", 4, "1f44d"},
        {"\x80", 1, "fffd"},
        {"\xC0\xAF", 2, "fffd fffd"},
        {"\xE0\x80\x80", 3, "fffd fffd fffd"},
        {"\xED\xA0\x80", 3, "fffd fffd fffd"},
        {"\xF0\x9F\x91", 3, "fffd"},
        {"\xF0\x9F\x91" "A", 4, "fffd 41"},
        {"\xE2\x86" "x", 3, "fffd 78"},
        {"\xF4\x90\x80\x80", 4, "fffd fffd fffd fffd"},
        {"\xF5", 1, "fffd"},
        {"a\xFF" "b", 3, "61 fffd 62"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) CHECK_S(cps(cases[i].in, cases[i].len), cases[i].want);
    size_t n;
    char *r = tui_utf8_lossy("a\xF0\x9F\x91" "b\xC3", 6, &n);
    CHECK_S(r, "a\xEF\xBF\xBD" "b\xEF\xBF\xBD");
    CHECK_U(n, 8);
    free(r);
    r = tui_utf8_lossy("ok \xEF\xBF\xBD", 6, NULL);
    CHECK_S(r, "ok \xEF\xBF\xBD");
    free(r);
    char enc[4];
    CHECK_U(tui_utf8_encode(0x1F44D, enc), 4);
    CHECK(memcmp(enc, "\xF0\x9F\x91\x8D", 4) == 0);
    CHECK_U(tui_utf8_encode(0xB5, enc), 2);
    CHECK(tui_utf8_valid("→ ok", strlen("→ ok")));
    CHECK(!tui_utf8_valid("\xC3", 1));
    CHECK_U(tui_utf8_count("→1 µs", strlen("→1 µs")), 5);
    return mesh_check_done();
}
