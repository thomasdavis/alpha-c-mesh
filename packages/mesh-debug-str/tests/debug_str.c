/* mesh-debug-str: the escapes Rust's {:?} uses (values printed by `format!("{:?}", s)`). */
#include "mesh/debug_str.h"
#include "mesh/check.h"

#include <stdlib.h>

static void is(const char *in, const char *want) {
    char *got = mdebug(in);
    CHECK_S(got, want);
    free(got);
}

int main(void) {
    is("x", "\"x\"");
    is("", "\"\"");
    is("a\"b\\c", "\"a\\\"b\\\\c\"");
    is("tab\there\nline\r", "\"tab\\there\\nline\\r\"");
    is("\x1b[0m", "\"\\u{1b}[0m\"");
    is("\x7f", "\"\\u{7f}\"");
    is("caf\xc3\xa9", "\"caf\xc3\xa9\"");
    msb b = {0};
    mdebug_str(&b, "a\0b", 3);
    CHECK_S(msb_str(&b), "\"a\\0b\"");
    msb_free(&b);
    return mesh_check_done();
}
