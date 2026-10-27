/* ratatui-core 0.1.2's Layout::try_split, ported constraint for constraint. The order constraints are
 * added in is ratatui's, so the solver pivots the same way. */
#include "tui/layout.h"

#include "tui/solver.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PRECISION 100.0 /* FLOAT_PRECISION_MULTIPLIER */

/* ratatui's strengths (layout.rs `mod strengths`). */
#define S_REQUIRED TUI_STRENGTH_REQUIRED
#define SPACER_SIZE_EQ (TUI_STRENGTH_REQUIRED / 10.0)
#define MIN_SIZE_GE (TUI_STRENGTH_STRONG * 100.0)
#define MAX_SIZE_LE (TUI_STRENGTH_STRONG * 100.0)
#define LENGTH_SIZE_EQ (TUI_STRENGTH_STRONG * 10.0)
#define PERCENTAGE_SIZE_EQ TUI_STRENGTH_STRONG
#define RATIO_SIZE_EQ (TUI_STRENGTH_STRONG / 10.0)
#define MIN_SIZE_EQ (TUI_STRENGTH_MEDIUM * 10.0)
#define MAX_SIZE_EQ (TUI_STRENGTH_MEDIUM * 10.0)
#define FILL_GROW TUI_STRENGTH_MEDIUM
#define GROW (TUI_STRENGTH_MEDIUM / 10.0)
#define SPACE_GROW (TUI_STRENGTH_WEAK * 10.0)
#define ALL_SEGMENT_GROW TUI_STRENGTH_WEAK
#define EMPTY_STRENGTH (TUI_STRENGTH_REQUIRED - TUI_STRENGTH_WEAK) /* Element::is_empty */

typedef struct {
    uint32_t start, end;
} element;

static tui_expr size(element e) { return tui_expr_sub(tui_expr_var(e.end), tui_expr_var(e.start)); }

static void add(tui_solver *s, tui_expr lhs, tui_relation op, double strength, tui_expr rhs) {
    (void)tui_solver_add(s, lhs, op, strength, rhs); /* ratatui's constraints are always satisfiable */
}

static void has_size(tui_solver *s, element e, tui_expr sz, double st) { add(s, size(e), TUI_EQ, st, sz); }
static void has_int_size(tui_solver *s, element e, uint32_t n, double st) {
    add(s, size(e), TUI_EQ, st, tui_expr_const((double)(uint16_t)n * PRECISION));
}
static void has_min_size(tui_solver *s, element e, int16_t n, double st) {
    add(s, size(e), TUI_GE, st, tui_expr_const((double)n * PRECISION));
}
static void has_max_size(tui_solver *s, element e, uint32_t n, double st) {
    add(s, size(e), TUI_LE, st, tui_expr_const((double)(uint16_t)n * PRECISION));
}
static void has_double_size(tui_solver *s, element e, tui_expr sz, double st) {
    add(s, size(e), TUI_EQ, st, tui_expr_scale(sz, 2.0));
}
static void is_empty(tui_solver *s, element e) { add(s, size(e), TUI_EQ, EMPTY_STRENGTH, tui_expr_const(0.0)); }

static void flex_constraints(tui_solver *s, element area, const element *sp, size_t nsp, tui_flex flex, int16_t spacing) {
    double spacing_f = (double)spacing * PRECISION;
    const element *mid = nsp > 2 ? sp + 1 : NULL;
    size_t nmid = nsp > 2 ? nsp - 2 : 0;
    switch (flex) {
    case TUI_FLEX_LEGACY:
        for (size_t i = 0; i < nmid; i++) has_size(s, mid[i], tui_expr_const(spacing_f), SPACER_SIZE_EQ);
        if (nsp) {
            is_empty(s, sp[0]);
            is_empty(s, sp[nsp - 1]);
        }
        break;
    case TUI_FLEX_SPACE_AROUND:
        if (nsp <= 2) {
            for (size_t i = 0; i < nsp; i++)
                for (size_t j = i + 1; j < nsp; j++) has_size(s, sp[i], size(sp[j]), SPACER_SIZE_EQ);
            for (size_t i = 0; i < nsp; i++) {
                has_min_size(s, sp[i], spacing, SPACER_SIZE_EQ);
                has_size(s, sp[i], size(area), SPACE_GROW);
            }
        } else {
            for (size_t i = 0; i < nmid; i++)
                for (size_t j = i + 1; j < nmid; j++) has_size(s, mid[i], size(mid[j]), SPACER_SIZE_EQ);
            has_double_size(s, mid[0], size(sp[0]), SPACER_SIZE_EQ);
            has_double_size(s, mid[0], size(sp[nsp - 1]), SPACER_SIZE_EQ);
            for (size_t i = 0; i < nsp; i++) {
                has_min_size(s, sp[i], spacing, SPACER_SIZE_EQ);
                has_size(s, sp[i], size(area), SPACE_GROW);
            }
        }
        break;
    case TUI_FLEX_SPACE_EVENLY:
        for (size_t i = 0; i < nsp; i++)
            for (size_t j = i + 1; j < nsp; j++) has_size(s, sp[i], size(sp[j]), SPACER_SIZE_EQ);
        for (size_t i = 0; i < nsp; i++) {
            has_min_size(s, sp[i], spacing, SPACER_SIZE_EQ);
            has_size(s, sp[i], size(area), SPACE_GROW);
        }
        break;
    case TUI_FLEX_SPACE_BETWEEN:
        for (size_t i = 0; i < nmid; i++)
            for (size_t j = i + 1; j < nmid; j++) has_size(s, mid[i], size(mid[j]), SPACER_SIZE_EQ);
        for (size_t i = 0; i < nmid; i++) {
            has_min_size(s, mid[i], spacing, SPACER_SIZE_EQ);
            has_size(s, mid[i], size(area), SPACE_GROW);
        }
        if (nsp) {
            is_empty(s, sp[0]);
            is_empty(s, sp[nsp - 1]);
        }
        break;
    case TUI_FLEX_START:
        for (size_t i = 0; i < nmid; i++) has_size(s, mid[i], tui_expr_const(spacing_f), SPACER_SIZE_EQ);
        if (nsp) {
            is_empty(s, sp[0]);
            has_size(s, sp[nsp - 1], size(area), GROW);
        }
        break;
    case TUI_FLEX_CENTER:
        for (size_t i = 0; i < nmid; i++) has_size(s, mid[i], tui_expr_const(spacing_f), SPACER_SIZE_EQ);
        if (nsp) {
            has_size(s, sp[0], size(area), GROW);
            has_size(s, sp[nsp - 1], size(area), GROW);
            has_size(s, sp[0], size(sp[nsp - 1]), SPACER_SIZE_EQ);
        }
        break;
    case TUI_FLEX_END:
        for (size_t i = 0; i < nmid; i++) has_size(s, mid[i], tui_expr_const(spacing_f), SPACER_SIZE_EQ);
        if (nsp) {
            is_empty(s, sp[nsp - 1]);
            has_size(s, sp[0], size(area), GROW);
        }
        break;
    }
}

static void segment_constraints(tui_solver *s, element area, const element *seg, const tui_constraint *c, size_t n,
                                tui_flex flex) {
    bool legacy = flex == TUI_FLEX_LEGACY;
    for (size_t i = 0; i < n; i++) {
        switch (c[i].kind) {
        case TUI_C_MAX:
            has_max_size(s, seg[i], c[i].a, MAX_SIZE_LE);
            has_int_size(s, seg[i], c[i].a, MAX_SIZE_EQ);
            break;
        case TUI_C_MIN:
            has_min_size(s, seg[i], (int16_t)(uint16_t)c[i].a, MIN_SIZE_GE);
            if (legacy) has_int_size(s, seg[i], c[i].a, MIN_SIZE_EQ);
            else has_size(s, seg[i], size(area), FILL_GROW);
            break;
        case TUI_C_LENGTH:
            has_int_size(s, seg[i], c[i].a, LENGTH_SIZE_EQ);
            break;
        case TUI_C_PERCENTAGE:
            has_size(s, seg[i], tui_expr_div(tui_expr_scale(size(area), (double)(uint16_t)c[i].a), 100.0),
                     PERCENTAGE_SIZE_EQ);
            break;
        case TUI_C_RATIO:
            has_size(s, seg[i], tui_expr_div(tui_expr_scale(size(area), (double)c[i].a), (double)(c[i].b ? c[i].b : 1)),
                     RATIO_SIZE_EQ);
            break;
        case TUI_C_FILL:
            has_size(s, seg[i], size(area), FILL_GROW);
            break;
        }
    }
}

static void fill_constraints(tui_solver *s, const element *seg, const tui_constraint *c, size_t n, tui_flex flex) {
    bool legacy = flex == TUI_FLEX_LEGACY;
    for (size_t i = 0; i < n; i++) {
        if (!(c[i].kind == TUI_C_FILL || (!legacy && c[i].kind == TUI_C_MIN))) continue;
        for (size_t j = i + 1; j < n; j++) {
            if (!(c[j].kind == TUI_C_FILL || (!legacy && c[j].kind == TUI_C_MIN))) continue;
            double lf = c[i].kind == TUI_C_FILL ? fmax((double)(uint16_t)c[i].a, 1e-6) : 1.0;
            double rf = c[j].kind == TUI_C_FILL ? fmax((double)(uint16_t)c[j].a, 1e-6) : 1.0;
            add(s, tui_expr_scale(size(seg[i]), rf), TUI_EQ, GROW, tui_expr_scale(size(seg[j]), lf));
        }
    }
}

static uint16_t to_u16(double v) {
    if (v != v || v <= 0) return 0;
    if (v >= 65535) return 65535;
    return (uint16_t)v;
}

static void solve(const tui_layout *l, tui_rect area, tui_rect *out, tui_rect *spacers_out) {
    size_t n = l->n, nvars = n * 2 + 2;
    tui_rect inner = tui_rect_inner(area, l->margin_h, l->margin_v);
    double start = (double)(l->vertical ? inner.y : inner.x) * PRECISION;
    double end = (double)(l->vertical ? tui_rect_bottom(inner) : tui_rect_right(inner)) * PRECISION;
    element *sp = malloc((n + 1) * sizeof *sp);
    element *seg = malloc((n ? n : 1) * sizeof *seg);
    if (!sp || !seg) abort();
    for (size_t i = 0; i < n + 1; i++) sp[i] = (element){(uint32_t)(2 * i), (uint32_t)(2 * i + 1)};
    for (size_t i = 0; i < n; i++) seg[i] = (element){(uint32_t)(2 * i + 1), (uint32_t)(2 * i + 2)};
    element areael = {0, (uint32_t)(nvars - 1)};
    tui_solver *s = tui_solver_new();
    add(s, tui_expr_var(areael.start), TUI_EQ, S_REQUIRED, tui_expr_const(start));
    add(s, tui_expr_var(areael.end), TUI_EQ, S_REQUIRED, tui_expr_const(end));
    for (uint32_t v = 0; v < nvars; v++) {
        add(s, tui_expr_var(v), TUI_GE, S_REQUIRED, tui_expr_var(areael.start));
        add(s, tui_expr_var(v), TUI_LE, S_REQUIRED, tui_expr_var(areael.end));
    }
    for (uint32_t v = 1; v + 1 < nvars; v += 2) add(s, tui_expr_var(v), TUI_LE, S_REQUIRED, tui_expr_var(v + 1));
    flex_constraints(s, areael, sp, n + 1, l->flex, l->spacing);
    segment_constraints(s, areael, seg, l->constraints, n, l->flex);
    fill_constraints(s, seg, l->constraints, n, l->flex);
    if (l->flex != TUI_FLEX_LEGACY)
        for (size_t i = 0; i + 1 < n; i++) has_size(s, seg[i], size(seg[i + 1]), ALL_SEGMENT_GROW);
    for (size_t i = 0; i < n + (spacers_out ? n + 1 : 0); i++) {
        element e = i < n ? seg[i] : sp[i - n];
        uint16_t a = to_u16(round(round(tui_solver_value(s, e.start)) / PRECISION));
        uint16_t b = to_u16(round(round(tui_solver_value(s, e.end)) / PRECISION));
        uint16_t sz = tui_sat_sub(b, a);
        tui_rect r = l->vertical ? (tui_rect){inner.x, a, inner.width, sz} : (tui_rect){a, inner.y, sz, inner.height};
        if (i < n) out[i] = r;
        else spacers_out[i - n] = r;
    }
    tui_solver_free(s);
    free(sp);
    free(seg);
}

/* ---- the cache: (area, layout) -> segments and spacers ---- */

enum { CACHE = 512 };

typedef struct {
    tui_rect area;
    tui_layout l;
    tui_constraint *c;
    tui_rect *rects; /* n segments then n + 1 spacers */
} entry;

static entry cache[CACHE];
static size_t cache_next;

static bool same(const entry *e, const tui_layout *l, tui_rect area) {
    return tui_rect_eq(e->area, area) && e->l.vertical == l->vertical && e->l.n == l->n && e->l.margin_h == l->margin_h &&
           e->l.margin_v == l->margin_v && e->l.flex == l->flex && e->l.spacing == l->spacing &&
           (l->n == 0 || memcmp(e->c, l->constraints, l->n * sizeof *l->constraints) == 0);
}

void tui_layout_cache_clear(void) {
    for (size_t i = 0; i < CACHE; i++) {
        free(cache[i].c);
        free(cache[i].rects);
        cache[i] = (entry){{0, 0, 0, 0}, {0}, NULL, NULL};
    }
    cache_next = 0;
}

void tui_layout_split(const tui_layout *l, tui_rect area, tui_rect *out, tui_rect *spacers) {
    for (size_t i = 0; i < CACHE; i++) {
        if (cache[i].rects && same(&cache[i], l, area)) {
            memcpy(out, cache[i].rects, l->n * sizeof *out);
            if (spacers) memcpy(spacers, cache[i].rects + l->n, (l->n + 1) * sizeof *spacers);
            return;
        }
    }
    tui_rect *all = malloc((2 * l->n + 1) * sizeof *all);
    if (!all) abort();
    solve(l, area, all, all + l->n);
    memcpy(out, all, l->n * sizeof *out);
    if (spacers) memcpy(spacers, all + l->n, (l->n + 1) * sizeof *spacers);
    entry *e = &cache[cache_next];
    cache_next = (cache_next + 1) % CACHE;
    free(e->c);
    free(e->rects);
    e->area = area;
    e->l = *l;
    e->c = malloc((l->n ? l->n : 1) * sizeof *e->c);
    if (!e->c) abort();
    if (l->n) memcpy(e->c, l->constraints, l->n * sizeof *e->c);
    e->l.constraints = e->c;
    e->rects = all;
}

void tui_vsplit(tui_rect area, size_t n, const tui_constraint *c, tui_rect *out) {
    tui_layout l = {.vertical = true, .constraints = c, .n = n, .flex = TUI_FLEX_START};
    tui_layout_split(&l, area, out, NULL);
}

void tui_hsplit(tui_rect area, size_t n, const tui_constraint *c, tui_rect *out) {
    tui_layout l = {.vertical = false, .constraints = c, .n = n, .flex = TUI_FLEX_START};
    tui_layout_split(&l, area, out, NULL);
}
