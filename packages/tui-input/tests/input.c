/* Byte sequences terminals send, decoded as crossterm decodes them. */
#include "mesh/check.h"
#include "tui/input.h"

#include <string.h>

static tui_key one(const char *s, bool quiet, size_t *used) {
    tui_key k;
    *used = tui_input_parse(s, strlen(s), quiet, &k);
    return k;
}

int main(void) {
    size_t n;
    tui_key k = one("q", false, &n);
    CHECK(k.code == TUI_KEY_CHAR && k.ch == 'q' && n == 1);
    k = one("L", false, &n);
    CHECK(k.code == TUI_KEY_CHAR && k.ch == 'L' && k.mods == TUI_MOD_SHIFT);
    k = one("\x03", false, &n);
    CHECK(k.code == TUI_KEY_CHAR && k.ch == 'c' && k.mods == TUI_MOD_CONTROL);
    CHECK(one("\x1b[A", false, &n).code == TUI_KEY_UP && n == 3);
    CHECK(one("\x1bOB", false, &n).code == TUI_KEY_DOWN && n == 3);
    CHECK(one("\x1b[5~", false, &n).code == TUI_KEY_PAGE_UP && n == 4);
    CHECK(one("\x1b[6~", false, &n).code == TUI_KEY_PAGE_DOWN);
    CHECK(one("\x1b[Z", false, &n).code == TUI_KEY_BACKTAB);
    k = one("\x1bOP", false, &n);
    CHECK(k.code == TUI_KEY_F && k.f == 1);
    k = one("\x1b[12~", false, &n);
    CHECK(k.code == TUI_KEY_F && k.f == 2);
    k = one("\x1b[1;5C", false, &n);
    CHECK(k.code == TUI_KEY_RIGHT && k.mods == TUI_MOD_CONTROL && n == 6);
    CHECK(one("\x7f", false, &n).code == TUI_KEY_BACKSPACE);
    CHECK(one("\r", false, &n).code == TUI_KEY_ENTER);
    CHECK(one("\t", false, &n).code == TUI_KEY_TAB);
    /* A lone ESC waits for quiet. */
    one("\x1b", false, &n);
    CHECK_U(n, 0);
    CHECK(one("\x1b", true, &n).code == TUI_KEY_ESC && n == 1);
    /* An incomplete CSI waits. */
    one("\x1b[1;", false, &n);
    CHECK_U(n, 0);
    /* UTF-8. */
    k = one("\xC3\xA9x", false, &n);
    CHECK(k.code == TUI_KEY_CHAR && k.ch == 0xE9 && n == 2);
    one("\xE2\x86", false, &n);
    CHECK_U(n, 0);
    /* Alt+x. */
    k = one("\x1bx", false, &n);
    CHECK(k.code == TUI_KEY_CHAR && k.ch == 'x' && (k.mods & TUI_MOD_ALT) && n == 2);
    /* Mouse reports are consumed and ignored. */
    k = one("\x1b[<0;10;5M", false, &n);
    CHECK(k.code == TUI_KEY_NONE && n == strlen("\x1b[<0;10;5M"));
    return mesh_check_done();
}
