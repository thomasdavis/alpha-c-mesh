/* Cassowary on the small problems kasuari's tests use: required equalities, a weak preference
 * overridden by a strong one, an inequality that clips, an unsatisfiable required pair, and a
 * chain of segments shared by required ordering constraints (as a layout makes). */
#include "mesh/check.h"
#include "tui/solver.h"

#include <math.h>

#define V(i) tui_expr_var(i)
#define C(x) tui_expr_const(x)

static bool near(double a, double b) { return fabs(a - b) < 1e-6; }

int main(void) {
    tui_solver *s = tui_solver_new();
    /* x + y == 10 (required), x == 3 (strong), y == 9 (weak): x = 3, y = 7. */
    CHECK(tui_solver_add(s, tui_expr_add(V(0), V(1)), TUI_EQ, TUI_STRENGTH_REQUIRED, C(10)));
    CHECK(tui_solver_add(s, V(0), TUI_EQ, TUI_STRENGTH_STRONG, C(3)));
    CHECK(tui_solver_add(s, V(1), TUI_EQ, TUI_STRENGTH_WEAK, C(9)));
    CHECK(near(tui_solver_value(s, 0), 3) && near(tui_solver_value(s, 1), 7));
    /* x <= 2 (required) overrides the strong preference. */
    CHECK(tui_solver_add(s, V(0), TUI_LE, TUI_STRENGTH_REQUIRED, C(2)));
    CHECK(near(tui_solver_value(s, 0), 2) && near(tui_solver_value(s, 1), 8));
    /* A required contradiction is refused (kasuari leaves the tableau as the failed pivot left it;
     * ratatui never adds one). */
    CHECK(!tui_solver_add(s, V(0), TUI_EQ, TUI_STRENGTH_REQUIRED, C(5)));
    CHECK(near(tui_solver_value(s, 42), 0));
    tui_solver_free(s);

    /* Two segments in [0, 100]: a == 30 strong, both grow medium, equal sizes weak. */
    s = tui_solver_new();
    CHECK(tui_solver_add(s, V(0), TUI_EQ, TUI_STRENGTH_REQUIRED, C(0)));
    CHECK(tui_solver_add(s, V(3), TUI_EQ, TUI_STRENGTH_REQUIRED, C(100)));
    for (uint32_t v = 0; v < 4; v++) {
        CHECK(tui_solver_add(s, V(v), TUI_GE, TUI_STRENGTH_REQUIRED, V(0)));
        CHECK(tui_solver_add(s, V(v), TUI_LE, TUI_STRENGTH_REQUIRED, V(3)));
    }
    CHECK(tui_solver_add(s, V(1), TUI_LE, TUI_STRENGTH_REQUIRED, V(2)));
    CHECK(tui_solver_add(s, tui_expr_sub(V(1), V(0)), TUI_EQ, TUI_STRENGTH_STRONG, C(30)));
    CHECK(tui_solver_add(s, tui_expr_sub(V(2), V(1)), TUI_EQ, TUI_STRENGTH_REQUIRED, C(0)));
    CHECK(tui_solver_add(s, tui_expr_sub(V(3), V(2)), TUI_EQ, TUI_STRENGTH_MEDIUM, tui_expr_sub(V(3), V(0))));
    CHECK(near(tui_solver_value(s, 1), 30) && near(tui_solver_value(s, 2), 30));
    tui_solver_free(s);
    return mesh_check_done();
}
