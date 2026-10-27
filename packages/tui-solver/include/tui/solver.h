/* @ajax/tui-solver: the Cassowary constraint solver (Badros, Borning, Stuckey), ported from kasuari
 * 0.4.13, the one ratatui lays out with. Constraints are linear expressions over variables, an
 * operator (<=, ==, >=) and a strength; required ones must hold, the others are met in order of
 * strength (lexicographically, through the weights), as nearly as the required ones allow.
 *
 * Variables are small integers the caller picks (0, 1, 2, ...). Every add re-optimises, as
 * kasuari's does, so the values are always the optimum of what was added so far. */
#ifndef TUI_SOLVER_H
#define TUI_SOLVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* kasuari's strengths. */
#define TUI_STRENGTH_REQUIRED 1001001000.0
#define TUI_STRENGTH_STRONG 1000000.0
#define TUI_STRENGTH_MEDIUM 1000.0
#define TUI_STRENGTH_WEAK 1.0

/* Strength::new: clamped to [0, REQUIRED]. */
static inline double tui_strength(double v) {
    return v < 0 ? 0 : v > TUI_STRENGTH_REQUIRED ? TUI_STRENGTH_REQUIRED : v;
}

typedef enum { TUI_LE, TUI_EQ, TUI_GE } tui_relation;

#define TUI_EXPR_TERMS 8

typedef struct {
    uint32_t var;
    double coeff;
} tui_term;

/* terms are kept in the order they were added (duplicates allowed, as kasuari's). */
typedef struct {
    tui_term terms[TUI_EXPR_TERMS];
    int n;
    double constant;
} tui_expr;

static inline tui_expr tui_expr_const(double c) {
    tui_expr e = {0};
    e.constant = c;
    return e;
}
static inline tui_expr tui_expr_var(uint32_t v) {
    tui_expr e = {0};
    e.terms[0] = (tui_term){v, 1.0};
    e.n = 1;
    return e;
}
/* a - b, b's terms negated and appended (Expression -= Expression). */
tui_expr tui_expr_sub(tui_expr a, tui_expr b);
tui_expr tui_expr_add(tui_expr a, tui_expr b);
tui_expr tui_expr_scale(tui_expr a, double k); /* k * a */
tui_expr tui_expr_div(tui_expr a, double k);   /* a / k, term by term */

typedef struct tui_solver tui_solver;

tui_solver *tui_solver_new(void);
void tui_solver_free(tui_solver *s);
/* lhs (op) rhs at strength: the constraint lhs - rhs (op) 0. False when it can't be satisfied
 * (kasuari's UnsatisfiableConstraint) or the solver hit an internal error. */
bool tui_solver_add(tui_solver *s, tui_expr lhs, tui_relation op, double strength, tui_expr rhs);
/* A variable's current value (0 for one the solver never saw). */
double tui_solver_value(const tui_solver *s, uint32_t var);

#endif
