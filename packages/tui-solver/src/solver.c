/* kasuari's Solver, Row and Symbol, ported. Rows are small arrays of (symbol, coefficient) kept in
 * insertion order; kasuari keeps them in hash maps, whose order only matters between equally
 * good pivots, which leave the optimum (and so the layout) the same. */
#include "tui/solver.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { K_INVALID, K_EXTERNAL, K_SLACK, K_ERROR, K_DUMMY } kind;

typedef struct {
    uint32_t id;
    uint8_t kind;
} symbol;

static const symbol INVALID = {0, K_INVALID};

static bool sym_eq(symbol a, symbol b) { return a.id == b.id && a.kind == b.kind; }

typedef struct {
    symbol sym;
    double coeff;
} cell;

typedef struct {
    cell *cells;
    size_t n, cap;
    double constant;
} row;

typedef struct {
    symbol sym; /* the basic symbol */
    row *row;
} basic;

typedef struct {
    uint32_t var;
    symbol sym;
    double value;
    size_t count;
} vardata;

struct tui_solver {
    vardata *vars;
    size_t nvars, capvars;
    basic *rows;
    size_t nrows, caprows;
    symbol *infeasible;
    size_t ninf, capinf;
    row objective;
    row *artificial;
    uint32_t id_tick;
};

static void *grow(void *p, size_t *cap, size_t need, size_t size) {
    if (need <= *cap) return p;
    size_t c = *cap ? *cap : 8;
    while (c < need) c *= 2;
    p = realloc(p, c * size);
    if (!p) abort();
    *cap = c;
    return p;
}

/* ---- expressions ---- */

static tui_expr expr_append(tui_expr a, tui_expr b, double sign) {
    for (int i = 0; i < b.n; i++) {
        if (a.n == TUI_EXPR_TERMS) {
            fputs("tui-solver: too many terms in an expression\n", stderr);
            abort();
        }
        a.terms[a.n++] = (tui_term){b.terms[i].var, sign * b.terms[i].coeff};
    }
    a.constant += sign * b.constant;
    return a;
}

tui_expr tui_expr_sub(tui_expr a, tui_expr b) { return expr_append(a, b, -1.0); }
tui_expr tui_expr_add(tui_expr a, tui_expr b) { return expr_append(a, b, 1.0); }
tui_expr tui_expr_div(tui_expr a, double k) {
    a.constant /= k;
    for (int i = 0; i < a.n; i++) a.terms[i].coeff /= k;
    return a;
}
tui_expr tui_expr_scale(tui_expr a, double k) {
    a.constant *= k;
    for (int i = 0; i < a.n; i++) a.terms[i].coeff *= k;
    return a;
}

/* ---- rows ---- */

static bool near_zero(double v) { return v < 0 ? -v < 1e-8 : v < 1e-8; }

static row *row_new(double constant) {
    row *r = calloc(1, sizeof *r);
    if (!r) abort();
    r->constant = constant;
    return r;
}

static void row_free(row *r) {
    if (!r) return;
    free(r->cells);
    free(r);
}

static row *row_clone(const row *r) {
    row *c = row_new(r->constant);
    c->cells = grow(NULL, &c->cap, r->n ? r->n : 1, sizeof *c->cells);
    memcpy(c->cells, r->cells, r->n * sizeof *c->cells);
    c->n = r->n;
    return c;
}

static long row_find(const row *r, symbol s) {
    for (size_t i = 0; i < r->n; i++)
        if (sym_eq(r->cells[i].sym, s)) return (long)i;
    return -1;
}

static void row_remove_at(row *r, size_t i) {
    memmove(&r->cells[i], &r->cells[i + 1], (r->n - i - 1) * sizeof *r->cells);
    r->n--;
}

static void row_insert_symbol(row *r, symbol s, double coeff) {
    long i = row_find(r, s);
    if (i < 0) {
        if (!near_zero(coeff)) {
            r->cells = grow(r->cells, &r->cap, r->n + 1, sizeof *r->cells);
            r->cells[r->n++] = (cell){s, coeff};
        }
        return;
    }
    r->cells[i].coeff += coeff;
    if (near_zero(r->cells[i].coeff)) row_remove_at(r, (size_t)i);
}

static bool row_insert_row(row *r, const row *other, double coeff) {
    double diff = other->constant * coeff;
    r->constant += diff;
    for (size_t i = 0; i < other->n; i++) row_insert_symbol(r, other->cells[i].sym, other->cells[i].coeff * coeff);
    return diff != 0.0;
}

static void row_remove(row *r, symbol s) {
    long i = row_find(r, s);
    if (i >= 0) row_remove_at(r, (size_t)i);
}

static void row_reverse_sign(row *r) {
    r->constant = -r->constant;
    for (size_t i = 0; i < r->n; i++) r->cells[i].coeff = -r->cells[i].coeff;
}

static void row_solve_for_symbol(row *r, symbol s) {
    long i = row_find(r, s);
    double c = r->cells[i].coeff;
    row_remove_at(r, (size_t)i);
    double coeff = -1.0 / c;
    r->constant *= coeff;
    for (size_t k = 0; k < r->n; k++) r->cells[k].coeff *= coeff;
}

static void row_solve_for_symbols(row *r, symbol lhs, symbol rhs) {
    row_insert_symbol(r, lhs, -1.0);
    row_solve_for_symbol(r, rhs);
}

static double row_coefficient(const row *r, symbol s) {
    long i = row_find(r, s);
    return i < 0 ? 0.0 : r->cells[i].coeff;
}

static bool row_substitute(row *r, symbol s, const row *with) {
    long i = row_find(r, s);
    if (i < 0) return false;
    double coeff = r->cells[i].coeff;
    row_remove_at(r, (size_t)i);
    return row_insert_row(r, with, coeff);
}

/* ---- the solver ---- */

tui_solver *tui_solver_new(void) {
    tui_solver *s = calloc(1, sizeof *s);
    if (!s) abort();
    s->id_tick = 1;
    return s;
}

void tui_solver_free(tui_solver *s) {
    if (!s) return;
    for (size_t i = 0; i < s->nrows; i++) row_free(s->rows[i].row);
    free(s->rows);
    free(s->vars);
    free(s->infeasible);
    free(s->objective.cells);
    row_free(s->artificial);
    free(s);
}

static long rows_find(const tui_solver *s, symbol sym) {
    for (size_t i = 0; i < s->nrows; i++)
        if (sym_eq(s->rows[i].sym, sym)) return (long)i;
    return -1;
}

static row *rows_get(const tui_solver *s, symbol sym) {
    long i = rows_find(s, sym);
    return i < 0 ? NULL : s->rows[i].row;
}

static void rows_insert(tui_solver *s, symbol sym, row *r) {
    long i = rows_find(s, sym);
    if (i >= 0) {
        row_free(s->rows[i].row);
        s->rows[i].row = r;
        return;
    }
    s->rows = grow(s->rows, &s->caprows, s->nrows + 1, sizeof *s->rows);
    s->rows[s->nrows++] = (basic){sym, r};
}

static row *rows_take(tui_solver *s, symbol sym) {
    long i = rows_find(s, sym);
    if (i < 0) return NULL;
    row *r = s->rows[i].row;
    memmove(&s->rows[i], &s->rows[i + 1], (s->nrows - (size_t)i - 1) * sizeof *s->rows);
    s->nrows--;
    return r;
}

static void push_infeasible(tui_solver *s, symbol sym) {
    s->infeasible = grow(s->infeasible, &s->capinf, s->ninf + 1, sizeof *s->infeasible);
    s->infeasible[s->ninf++] = sym;
}

static symbol var_symbol(tui_solver *s, uint32_t var) {
    for (size_t i = 0; i < s->nvars; i++)
        if (s->vars[i].var == var) {
            s->vars[i].count++;
            return s->vars[i].sym;
        }
    s->vars = grow(s->vars, &s->capvars, s->nvars + 1, sizeof *s->vars);
    symbol sym = {s->id_tick++, K_EXTERNAL};
    s->vars[s->nvars++] = (vardata){var, sym, NAN, 1};
    return sym;
}

typedef struct {
    symbol marker, other;
} tag;

static row *create_row(tui_solver *s, const tui_expr *e, tui_relation op, double strength, tag *t) {
    row *r = row_new(e->constant);
    for (int i = 0; i < e->n; i++) {
        if (near_zero(e->terms[i].coeff)) continue;
        symbol sym = var_symbol(s, e->terms[i].var);
        row *other = rows_get(s, sym);
        if (other) row_insert_row(r, other, e->terms[i].coeff);
        else row_insert_symbol(r, sym, e->terms[i].coeff);
    }
    if (op == TUI_LE || op == TUI_GE) {
        double coeff = op == TUI_LE ? 1.0 : -1.0;
        symbol slack = {s->id_tick++, K_SLACK};
        row_insert_symbol(r, slack, coeff);
        if (strength < TUI_STRENGTH_REQUIRED) {
            symbol error = {s->id_tick++, K_ERROR};
            row_insert_symbol(r, error, -coeff);
            row_insert_symbol(&s->objective, error, strength);
            *t = (tag){slack, error};
        } else {
            *t = (tag){slack, INVALID};
        }
    } else if (strength < TUI_STRENGTH_REQUIRED) {
        symbol errplus = {s->id_tick++, K_ERROR};
        symbol errminus = {s->id_tick++, K_ERROR};
        row_insert_symbol(r, errplus, -1.0);
        row_insert_symbol(r, errminus, 1.0);
        row_insert_symbol(&s->objective, errplus, strength);
        row_insert_symbol(&s->objective, errminus, strength);
        *t = (tag){errplus, errminus};
    } else {
        symbol dummy = {s->id_tick++, K_DUMMY};
        row_insert_symbol(r, dummy, 1.0);
        *t = (tag){dummy, INVALID};
    }
    if (r->constant < 0.0) row_reverse_sign(r);
    return r;
}

static symbol choose_subject(const row *r, const tag *t) {
    for (size_t i = 0; i < r->n; i++)
        if (r->cells[i].sym.kind == K_EXTERNAL) return r->cells[i].sym;
    if ((t->marker.kind == K_SLACK || t->marker.kind == K_ERROR) && row_coefficient(r, t->marker) < 0.0) return t->marker;
    if ((t->other.kind == K_SLACK || t->other.kind == K_ERROR) && row_coefficient(r, t->other) < 0.0) return t->other;
    return INVALID;
}

static bool all_dummies(const row *r) {
    for (size_t i = 0; i < r->n; i++)
        if (r->cells[i].sym.kind != K_DUMMY) return false;
    return true;
}

static void substitute(tui_solver *s, symbol sym, const row *r) {
    for (size_t i = 0; i < s->nrows; i++) {
        row_substitute(s->rows[i].row, sym, r);
        if (s->rows[i].sym.kind != K_EXTERNAL && s->rows[i].row->constant < 0.0) push_infeasible(s, s->rows[i].sym);
    }
    row_substitute(&s->objective, sym, r);
    if (s->artificial) row_substitute(s->artificial, sym, r);
}

static symbol entering_symbol(const row *objective) {
    for (size_t i = 0; i < objective->n; i++)
        if (objective->cells[i].sym.kind != K_DUMMY && objective->cells[i].coeff < 0.0) return objective->cells[i].sym;
    return INVALID;
}

static bool leaving_row(tui_solver *s, symbol entering, symbol *leaving, row **r) {
    double ratio = INFINITY;
    long found = -1;
    for (size_t i = 0; i < s->nrows; i++) {
        if (s->rows[i].sym.kind == K_EXTERNAL) continue;
        double temp = row_coefficient(s->rows[i].row, entering);
        if (temp < 0.0) {
            double tr = -s->rows[i].row->constant / temp;
            if (tr < ratio) {
                ratio = tr;
                found = (long)i;
            }
        }
    }
    if (found < 0) return false;
    *leaving = s->rows[found].sym;
    *r = rows_take(s, *leaving);
    return true;
}

static bool optimize(tui_solver *s, bool artificial) {
    for (;;) {
        symbol entering = entering_symbol(artificial ? s->artificial : &s->objective);
        if (entering.kind == K_INVALID) return true;
        symbol leaving;
        row *r;
        if (!leaving_row(s, entering, &leaving, &r)) return false; /* objective unbounded */
        row_solve_for_symbols(r, leaving, entering);
        substitute(s, entering, r);
        rows_insert(s, entering, r);
    }
}

static symbol any_pivotable_symbol(const row *r) {
    for (size_t i = 0; i < r->n; i++)
        if (r->cells[i].sym.kind == K_SLACK || r->cells[i].sym.kind == K_ERROR) return r->cells[i].sym;
    return INVALID;
}

static int add_with_artificial_variable(tui_solver *s, const row *r) {
    symbol art = {s->id_tick++, K_SLACK};
    rows_insert(s, art, row_clone(r));
    s->artificial = row_clone(r);
    if (!optimize(s, true)) return -1;
    bool success = near_zero(s->artificial->constant);
    row_free(s->artificial);
    s->artificial = NULL;
    row *ar = rows_take(s, art);
    if (ar) {
        if (ar->n == 0) {
            row_free(ar);
            return success;
        }
        symbol entering = any_pivotable_symbol(ar);
        if (entering.kind == K_INVALID) {
            row_free(ar);
            return 0;
        }
        row_solve_for_symbols(ar, art, entering);
        substitute(s, entering, ar);
        rows_insert(s, entering, ar);
    }
    for (size_t i = 0; i < s->nrows; i++) row_remove(s->rows[i].row, art);
    row_remove(&s->objective, art);
    return success;
}

bool tui_solver_add(tui_solver *s, tui_expr lhs, tui_relation op, double strength, tui_expr rhs) {
    tui_expr e = tui_expr_sub(lhs, rhs);
    tag t;
    row *r = create_row(s, &e, op, strength, &t);
    symbol subject = choose_subject(r, &t);
    if (subject.kind == K_INVALID && all_dummies(r)) {
        if (!near_zero(r->constant)) {
            row_free(r);
            return false;
        }
        subject = t.marker;
    }
    if (subject.kind == K_INVALID) {
        int ok = add_with_artificial_variable(s, r);
        row_free(r);
        if (ok != 1) return false;
    } else {
        row_solve_for_symbol(r, subject);
        substitute(s, subject, r);
        rows_insert(s, subject, r);
    }
    return optimize(s, false);
}

double tui_solver_value(const tui_solver *s, uint32_t var) {
    for (size_t i = 0; i < s->nvars; i++)
        if (s->vars[i].var == var) {
            row *r = rows_get(s, s->vars[i].sym);
            return r ? r->constant : 0.0;
        }
    return 0.0;
}
