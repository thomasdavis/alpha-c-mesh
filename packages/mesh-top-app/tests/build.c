/* Builds and links; the behaviour is tested against the Rust in @ajax/mesh-top-golden. Here: the
 * key bindings that need no snapshot. */
#include "mesh/check.h"
#include "mesh/top_app.h"

#include <stdlib.h>

int main(void) {
    const char *eps[2] = {"a:1", "b:2"};
    mt_app app;
    mt_app_init(&app, eps, 2);
    mt_action a = mt_app_handle_key(&app, tui_key_char(']'));
    CHECK(a.kind == MT_ACTION_NONE && app.focus == 1);
    a = mt_app_handle_key(&app, tui_key_char('9'));
    CHECK(app.tab == MT_TAB_FLEET);
    mt_action t[2];
    CHECK_U(mt_app_tick(&app, 0, t), 1);
    CHECK(t[0].kind == MT_ACTION_POLL && t[0].machine == 1);
    CHECK_S(t[0].line, "view fleet");
    mt_action_free(&t[0]);
    CHECK_U(mt_app_tick(&app, MT_MS(500), t), 0);
    tui_key c = tui_key_char('c');
    c.mods = TUI_MOD_CONTROL;
    CHECK(mt_app_handle_key(&app, c).kind == MT_ACTION_QUIT);
    mt_app_free(&app);
    return mesh_check_done();
}
