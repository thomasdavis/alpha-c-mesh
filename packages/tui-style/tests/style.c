/* Style patching as ratatui's Style::patch tests specify it. */
#include "mesh/check.h"
#include "tui/style.h"

int main(void) {
    tui_style a = tui_style_add(tui_fg(TUI_RED), TUI_BOLD | TUI_ITALIC);
    tui_style b = tui_style_remove(tui_style_bg(tui_style_new(), TUI_BLUE), TUI_ITALIC);
    tui_style p = tui_style_patch(a, b);
    CHECK(p.has_fg && tui_color_eq(p.fg, TUI_RED));
    CHECK(p.has_bg && tui_color_eq(p.bg, TUI_BLUE));
    CHECK_U(p.add, TUI_BOLD);
    CHECK_U(p.sub, TUI_ITALIC);
    /* An empty style changes nothing. */
    CHECK(tui_style_eq(tui_style_patch(a, tui_style_new()), a));
    /* add after remove clears the remove. */
    tui_style c = tui_style_add(tui_style_remove(tui_style_new(), TUI_BOLD), TUI_BOLD);
    CHECK_U(c.add, TUI_BOLD);
    CHECK_U(c.sub, 0);
    CHECK(!tui_color_eq(TUI_RGB(1, 2, 3), TUI_RGB(1, 2, 4)));
    CHECK(tui_color_eq(TUI_WHITE, TUI_NAMED(15)));
    return mesh_check_done();
}
