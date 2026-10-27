/* ui_fleet.rs and ui.rs's draw_fleet_snap, ported. */
#include "mesh/top_ui_fleet.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *mt_bar(mt_frame *f, double frac, size_t w) {
    double fr = isfinite(frac) ? (frac < 0.0 ? 0.0 : frac > 1.0 ? 1.0 : frac) : 0.0;
    size_t eighths = (size_t)mt_sat_u64(round(fr * (double)(w * 8)));
    static const char *const part[8] = {" ", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};
    char *o = tui_arena_alloc(f->a, 3 * (w + eighths / 8 + 2) + 1);
    size_t chars = 0;
    for (size_t i = 0; i < eighths / 8; i++, chars++) strcat(o, "█");
    if (eighths % 8 != 0 && chars < w) {
        strcat(o, part[eighths % 8]);
        chars++;
    }
    while (chars < w) {
        strcat(o, " ");
        chars++;
    }
    return o;
}

const char *mt_cut(mt_frame *f, const char *s, size_t w) {
    if (rsfmt_chars(s) <= w) return tui_arena_strdup(f->a, s);
    if (w == 0) return "";
    return MT_FMT(f, "%s…", mt_take(f, s, w - 1));
}

/* Rows of area, top to bottom: each a height (cut to what is left), -1 the rest (at most one). */
static void split_v(tui_rect area, const int *hs, size_t n, tui_rect *out) {
    uint32_t fixed = 0;
    for (size_t i = 0; i < n; i++)
        if (hs[i] >= 0) fixed += (uint32_t)hs[i];
    uint16_t rest = area.height > fixed ? (uint16_t)(area.height - fixed) : 0;
    uint16_t y = area.y, end = (uint16_t)(area.y + area.height);
    for (size_t i = 0; i < n; i++) {
        uint16_t h = hs[i] >= 0 ? (uint16_t)hs[i] : rest;
        if (h > end - y) h = (uint16_t)(end - y);
        out[i] = mt_rect(area.x, y, area.width, h);
        y = (uint16_t)(y + h);
    }
}

static const char *secs_text(mt_frame *f, double x) {
    char a[64], b[64];
    if (x >= 3600.0) {
        rsfmt_fixed(a, sizeof a, floor(x / 3600.0), 0, 0);
        rsfmt_fixed_w(b, sizeof b, fmod(x, 3600.0) / 60.0, 0, 2, '>', RSFMT_ZERO);
        return MT_FMT(f, "%sh%sm", a, b);
    }
    if (x >= 60.0) {
        rsfmt_fixed(a, sizeof a, floor(x / 60.0), 0, 0);
        rsfmt_fixed_w(b, sizeof b, fmod(x, 60.0), 0, 2, '>', RSFMT_ZERO);
        return MT_FMT(f, "%sm%ss", a, b);
    }
    return MT_FMT(f, "%ss", mt_ff(f, x, 1));
}

static const char *pct3(mt_frame *f, double v) {
    char b[64];
    rsfmt_fixed_w(b, sizeof b, v, 0, 3, '>', 0);
    return MT_FMT(f, " %s%%", b);
}

/* ---- view records */

static const mt_vrec EMPTY_REC = {"", NULL, 0, ""};

static const char *rget(const mt_vrec *r, const char *k) { return mt_vrec_get(r, k); }
static const char *rs(const mt_vrec *r, const char *k) { return mt_vrec_s(r, k); }
static bool ru(const mt_vrec *r, const char *k, uint64_t *v) { return mt_vrec_u(r, k, v); }
static bool rf(const mt_vrec *r, const char *k, double *v) { return mt_vrec_f(r, k, v); }
static bool streq_opt(const char *a, const char *b) { return a == b || (a && b && strcmp(a, b) == 0); }

/* The records of one kind. */
static size_t all(mt_frame *f, const mt_view *v, const char *kind, const mt_vrec ***out) {
    const mt_vrec **o = tui_arena_alloc(f->a, (v->n ? v->n : 1) * sizeof *o);
    size_t n = 0;
    for (size_t i = 0; i < v->n; i++)
        if (strcmp(v->recs[i].kind, kind) == 0) o[n++] = &v->recs[i];
    *out = o;
    return n;
}

static tui_span source(mt_frame *f, const mt_app *app, const mt_view *v, mt_instant now) {
    const char *m = v->machine < app->nmachines ? mt_name(f, &app->machines[v->machine]) : "?";
    if (v->err) return mt_fgs(MT_FMT(f, " %s: %s ", m, v->err), MT_BAD);
    mt_duration d = now > v->at ? now - v->at : 0;
    return mt_label(MT_FMT(f, " view of %s, %s s ago ", m, mt_ff(f, mt_secs_f64(d), 1)));
}

static void par_wrap(mt_frame *f, tui_text t, const tui_block *b, tui_rect area) {
    tui_paragraph p = tui_paragraph_new(t);
    p.wrap = TUI_WRAP_TRIM;
    p.block = b;
    tui_paragraph_render(f->a, &p, area, f->buf);
}

static void par_block(mt_frame *f, tui_text t, const tui_block *b, tui_rect area) {
    tui_paragraph p = tui_paragraph_new(t);
    p.block = b;
    tui_paragraph_render(f->a, &p, area, f->buf);
}

/* Waiting for a view: which machine is asked. */
static void waiting(mt_frame *f, tui_rect area, const mt_app *app, size_t view) {
    size_t mi = mt_app_view_machine(app, view);
    const char *m = mi < app->nmachines ? mt_name(f, &app->machines[mi]) : "?";
    const char *vn = MT_VIEWS[view];
    tui_line t = MT_LINE(f, mt_label("asking "), mt_val(m),
                         mt_label(MT_FMT(f, " for `view %s` (meshd's %s view; http /%s serves the same)", vn, vn, vn)));
    tui_block b = mt_panel(f, tui_line_span(f->a, mt_label(MT_FMT(f, " %s ", vn))), MT_FAINT);
    par_wrap(f, tui_text_line(f->a, t), &b, area);
}

static tui_style vstate_style(const char *s) {
    static const char *const good[] = {"up", "running", "in-sync", "chosen", "eligible", "done"};
    static const char *const warn[] = {"suspect", "pending", "linked", "hello", "feasible", "cancelling", "leader-merging"};
    static const char *const bad[] = {"down", "failed", "paused", "rejected", "deadline", "done-with-failures", "cancelled"};
    for (size_t i = 0; i < sizeof good / sizeof *good; i++)
        if (strcmp(s, good[i]) == 0) return tui_fg(MT_GOOD);
    for (size_t i = 0; i < sizeof warn / sizeof *warn; i++)
        if (strcmp(s, warn[i]) == 0) return tui_fg(MT_WARN);
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        if (strcmp(s, bad[i]) == 0) return tui_fg(MT_BAD);
    return tui_fg(MT_DIM);
}

static const char *mb_gb(mt_frame *f, const mt_vrec *r, const char *k) {
    uint64_t m;
    return ru(r, k, &m) ? mt_ff(f, (double)m / 1024.0, 0) : "-";
}

static void draw_table(mt_frame *f, const mt_rows *rows, const char *const *header, size_t nh, const tui_constraint *w, size_t nw,
                       const tui_block *b, tui_rect area, tui_table_state *st) {
    tui_table_row hdr = mt_header(f, header, nh);
    tui_table t = tui_table_new(rows->rows, rows->n, w, nw);
    t.header = &hdr;
    t.column_spacing = 1;
    t.block = b;
    tui_table_state none = {0};
    tui_table_render(f->a, &t, area, f->buf, st ? st : &none);
}

/* ---------------------------------------------------------------- 9 fleet */

/* The placement matrix's cell: a glyph (running ●, placed but not running ○, running but not
 * placed ◌, nothing ·), the executor's letter, then flags. */
static const char *cell_text(mt_frame *f, const mt_vrec *c, tui_style *st) {
    bool placed = mt_vrec_flag(c, "placed");
    const char *state = rs(c, "state");
    bool running = strcmp(state, "running") == 0;
    const char *glyph = placed && running ? "●" : placed ? "○" : (running || strcmp(state, "stopped") == 0) ? "◌" : "·";
    const char *e = rget(c, "exec");
    char exec = !e ? ' ' : !strcmp(e, "cuda") ? 'C' : !strcmp(e, "metal") ? 'M' : !strcmp(e, "wasm") ? 'W' : !strcmp(e, "native") ? 'N' : ' ';
    char flags[8];
    size_t n = 0;
    if (mt_vrec_flag(c, "pinned")) flags[n++] = 'p';
    const char *mv = rget(c, "moving");
    if (mv && !strcmp(mv, "to")) flags[n++] = '>';
    else if (mv && !strcmp(mv, "from")) flags[n++] = '<';
    if (rget(c, "reserved")) flags[n++] = 'r';
    flags[n] = 0;
    if (mv) *st = tui_fg_bold(MT_ACCENT);
    else if (placed && running) *st = tui_fg(MT_GOOD);
    else if (placed) *st = tui_fg(MT_WARN);
    else if (running) *st = tui_fg(MT_BAD);
    else *st = tui_fg(MT_FAINT);
    return MT_FMT(f, "%s%c%s", glyph, exec, mt_pad(f, mt_cut(f, flags, 2), 2, '<'));
}

void mt_draw_fleet(mt_frame *f, tui_rect area, const mt_app *app, mt_instant now) {
    const mt_view *v = mt_app_view(app, 0);
    if (!v) {
        if (area.height >= 12) {
            tui_rect r[2];
            tui_vsplit(area, 2, (tui_constraint[]){TUI_LENGTH(3), TUI_FILL(1)}, r);
            waiting(f, r[0], app, 0);
            mt_draw_fleet_snap(f, r[1], app);
        } else {
            waiting(f, area, app, 0);
        }
        return;
    }
    const mt_vrec *head = mt_view_first(v, "fleet");
    if (!head) head = &EMPTY_REC;
    bool single = streq_opt(rget(head, "role"), "single");
    const mt_vrec **members, **progs;
    size_t nmem = all(f, v, "member", &members), nprog = all(f, v, "program", &progs);
    char *sel_p;
    bool has_sel_m;
    uint32_t sel_m = 0;
    mt_app_fleet_sel(app, &sel_p, &has_sel_m, &sel_m);
    const char *sel_pa = sel_p ? tui_arena_strdup(f->a, sel_p) : NULL;
    free(sel_p);
    /* The headline: the leader, the desired state, the scheduler's switches. */
    uint64_t leader;
    bool has_leader = ru(head, "leader", &leader);
    tui_line l1 = MT_LINE(f, mt_label("leader "),
                          has_leader ? mt_fgb(MT_FMT(f, "%u", (uint32_t)leader), mt_sys_color((uint32_t)leader)) : mt_fgb("none", MT_BAD),
                          mt_label(" term "), mt_val(rs(head, "term")), mt_label(" version "), mt_val(rs(head, "version")), mt_label(" applied "),
                          mt_fgs(rs(head, "applied"), streq_opt(rget(head, "applied"), rget(head, "version")) ? MT_GOOD : MT_WARN),
                          mt_label(" quorum "), mt_val(rs(head, "quorum")), mt_label(" up "), mt_val(rs(head, "up")), mt_label(" suspect "),
                          mt_fgs(rs(head, "suspect"), rget(head, "suspect") ? MT_WARN : MT_DIM), mt_label(" majority "),
                          mt_styled(rs(head, "majority"), streq_opt(rget(head, "majority"), "no") ? tui_fg_bold(MT_BAD) : tui_fg(MT_GOOD)));
    if (single || area.height < 6 || area.width < 40) {
        /* Tiny (or a single meshd): the headline only. */
        mt_lines lines = {0};
        mt_lines_push(f, &lines, l1);
        if (single) {
            mt_lines_push(f, &lines, tui_line_span(f->a, mt_label(head->text)));
        } else {
            size_t up = 0;
            for (size_t i = 0; i < nmem; i++) up += streq_opt(rget(members[i], "state"), "up");
            mt_lines_push(f, &lines, MT_LINE(f, mt_label("members up "), mt_val(MT_FMT(f, "%zu/%zu", up, nmem)), mt_label("  programs "),
                                             mt_val(MT_FMT(f, "%zu", nprog))));
        }
        par_wrap(f, mt_text(f, &lines), NULL, area);
        return;
    }
    bool paused = mt_vrec_flag(head, "paused");
    const char *gap = rget(head, "last_gap_ms");
    tui_line l2 = MT_LINE(f, mt_label("scheduler "), mt_styled(paused ? "PAUSED" : "running", paused ? tui_fg_bold(MT_BAD) : tui_fg(MT_GOOD)),
                          mt_label(" autoreform "), mt_val(mt_vrec_flag(head, "autoreform") ? "on" : "off"), mt_label(" cordon "),
                          mt_styled(rs(head, "cordon"), rget(head, "cordon") ? tui_fg_bold(MT_WARN) : tui_fg(MT_DIM)), mt_label(" busy "),
                          mt_styled(rs(head, "busy_members"), rget(head, "busy_members") ? tui_fg_bold(MT_WARN) : tui_fg(MT_DIM)),
                          mt_label(" migrations "), mt_val(rs(head, "migrations")), mt_label(" last gap "),
                          mt_val(gap ? MT_FMT(f, "%s ms", gap) : "-"), source(f, app, v, now));
    const mt_vrec **groups;
    size_t ngroups = all(f, v, "group", &groups);
    int want_groups = ngroups == 0 ? 0 : (int)(ngroups * 2 + 2 < 6 ? ngroups * 2 + 2 : 6);
    tui_rect sp[4];
    split_v(area, (int[]){2, (int)nmem + 3, -1, area.height >= 24 ? want_groups : 0}, 4, sp);
    tui_rect hd = sp[0], mem = sp[1], mid = sp[2], grp = sp[3];
    mt_par(f, tui_text_of(f->a, 2, (tui_line[]){l1, l2}), hd);
    /* The members: link state, flags, executor and caps, the smoothed load, reconcile. */
    bool wide = area.width >= 96;
    const char *header[12];
    size_t nh = 0;
    const char *h0[5] = {"sys", "host", "state", "flags", "exec"};
    for (size_t i = 0; i < 5; i++) header[nh++] = h0[i];
    if (wide) {
        header[nh++] = "cpus";
        header[nh++] = "mem GB";
    }
    header[nh++] = "gpu share";
    header[nh++] = "cpu";
    header[nh++] = "psi";
    header[nh++] = "reconcile (applied) / why";
    mt_rows rows = {0};
    for (size_t i = 0; i < nmem; i++) {
        const mt_vrec *m = members[i];
        uint64_t id = 0;
        ru(m, "id", &id);
        uint32_t k = (uint32_t)id;
        char flags[4];
        size_t fl = 0;
        if (mt_vrec_flag(m, "leader")) flags[fl++] = 'L';
        if (mt_vrec_flag(m, "cordoned")) flags[fl++] = 'C';
        if (mt_vrec_flag(m, "busy")) flags[fl++] = 'B';
        flags[fl] = 0;
        mt_cells cells = {0};
        mt_cells_push(f, &cells, mt_cell_span(f, mt_fgb(MT_FMT(f, "%u", k), mt_sys_color(k))));
        mt_cells_push(f, &cells, mt_cell_str(f, mt_cut(f, rs(m, "host"), 14)));
        mt_cells_push(f, &cells, mt_cell_span(f, mt_styled(rs(m, "state"), vstate_style(rs(m, "state")))));
        mt_cells_push(f, &cells, mt_cell_span(f, mt_fgb(tui_arena_strdup(f->a, flags),
                                                        mt_vrec_flag(m, "cordoned") || mt_vrec_flag(m, "busy") ? MT_WARN : MT_ACCENT)));
        mt_cells_push(f, &cells, mt_cell_str(f, rs(m, "gpu")));
        if (wide) {
            mt_cells_push(f, &cells, mt_cell_str(f, MT_FMT(f, "%s/%s", rs(m, "big"), rs(m, "little"))));
            mt_cells_push(f, &cells, mt_cell_str(f, MT_FMT(f, "%s/%s", mb_gb(f, m, "avail_mb"), mb_gb(f, m, "ram_mb"))));
        }
        double x;
        if (rf(m, "share", &x))
            mt_cells_push(f, &cells, mt_cell_line(f, MT_LINE(f, mt_styled(mt_bar(f, x, 5), mt_share_style((float)x)),
                                                               mt_styled(MT_FMT(f, " %s", mt_pct(f, (float)x)), mt_share_style((float)x)))));
        else
            mt_cells_push(f, &cells, mt_cell_line(f, tui_line_span(f->a, mt_fgs("-", MT_FAINT))));
        mt_cells_push(f, &cells, mt_cell_span(f, rf(m, "cpu", &x) ? mt_styled(mt_pct(f, (float)x), mt_busy_style((float)x)) : mt_fgs("-", MT_FAINT)));
        mt_cells_push(f, &cells, mt_cell_span(f, rf(m, "psi_cpu", &x) ? mt_styled(mt_ff(f, x, 2), mt_busy_style((float)(x * 4.0))) : mt_fgs("-", MT_FAINT)));
        const char *rec = rs(m, "reconcile");
        tui_line rc = MT_LINE(f, mt_styled(rec, vstate_style(rec)), mt_label(MT_FMT(f, " (%s)", rs(m, "applied"))));
        if (*m->text) tui_line_push(f->a, &rc, mt_fgs(MT_FMT(f, " %s", m->text), mt_vrec_flag(m, "busy") ? MT_WARN : MT_DIM));
        mt_cells_push(f, &cells, mt_cell_line(f, rc));
        tui_table_row row = mt_row(f, cells.c, cells.n);
        if (has_sel_m && sel_m == k) row.style = tui_style_bg(tui_style_new(), MT_FAINT);
        mt_rows_push(f, &rows, row);
    }
    tui_constraint widths[12];
    size_t nw = 0;
    const uint16_t w0[5] = {3, 14, 7, 5, 6};
    for (size_t i = 0; i < 5; i++) widths[nw++] = TUI_LENGTH(w0[i]);
    if (wide) {
        widths[nw++] = TUI_LENGTH(5);
        widths[nw++] = TUI_LENGTH(7);
    }
    widths[nw++] = TUI_LENGTH(10);
    widths[nw++] = TUI_LENGTH(4);
    widths[nw++] = TUI_LENGTH(5);
    widths[nw++] = TUI_FILL(1);
    tui_block mb = mt_panel(f, MT_LINE(f, mt_fgb(" members ", MT_FG), mt_label("L leader  C cordoned  B busy (owner policy) ")), MT_FAINT);
    draw_table(f, &rows, header, nh, widths, nw, &mb, mem, NULL);
    if (mid.height < 3) return;
    /* The placement matrix (programs x members) and, beside it, the selected program's explain
     * and the decisions. */
    uint32_t *ids = tui_arena_alloc(f->a, (nmem ? nmem : 1) * sizeof *ids);
    size_t nids = 0;
    for (size_t i = 0; i < nmem; i++) {
        uint64_t id;
        if (ru(members[i], "id", &id)) ids[nids++] = (uint32_t)id;
    }
    size_t name_w = 7;
    if (nprog) {
        name_w = 0;
        for (size_t i = 0; i < nprog; i++) {
            size_t c = rsfmt_chars(rs(progs[i], "name"));
            if (c > name_w) name_w = c;
        }
    }
    if (name_w < 7) name_w = 7;
    if (name_w > 14) name_w = 14;
    uint16_t mat_w = tui_min16((uint16_t)(name_w + 2 + 5 * nids + 6), mid.width);
    bool side = mid.width >= mat_w + 30;
    tui_rect hs[2];
    tui_hsplit(mid, 2, (tui_constraint[]){TUI_LENGTH(side ? mat_w : mid.width), TUI_FILL(1)}, hs);
    tui_rect mat = hs[0], why = hs[1];
    mt_lines lines = {0};
    tui_line hl = tui_line_span(f->a, mt_fgs(mt_pad(f, "program", (int)name_w + 1, '<'), MT_DIM));
    for (size_t i = 0; i < nids; i++) tui_line_push(f->a, &hl, mt_fgb(mt_pad(f, MT_FMT(f, "%u", ids[i]), 5, '<'), mt_sys_color(ids[i])));
    mt_lines_push(f, &lines, hl);
    size_t rows_fit = tui_sat_sub(mat.height, 3);
    size_t a = rows_fit > 0 ? rows_fit - 1 : 0;
    size_t first = app->fsel[0] > a ? app->fsel[0] - a : 0;
    size_t cap = nprog > (rows_fit > 1 ? rows_fit : 1) ? nprog - (rows_fit > 1 ? rows_fit : 1) : 0;
    if (first > cap) first = cap;
    const mt_vrec **cells_r;
    size_t ncells = all(f, v, "cell", &cells_r);
    for (size_t pi = first; pi < nprog && pi < first + rows_fit; pi++) {
        const char *name = rs(progs[pi], "name");
        bool selp = sel_pa && strcmp(sel_pa, name) == 0;
        tui_line l = tui_line_span(f->a, mt_styled(MT_FMT(f, "%s ", mt_pad(f, mt_cut(f, name, name_w), (int)name_w, '<')),
                                                   selp ? mt_fg_bg_bold(MT_BG, MT_ACCENT) : tui_fg(MT_FG)));
        for (size_t ki = 0; ki < nids; ki++) {
            const mt_vrec *c = NULL;
            for (size_t q = 0; q < ncells && !c; q++) {
                uint64_t mm;
                if (streq_opt(rget(cells_r[q], "program"), name) && ru(cells_r[q], "member", &mm) && mm == ids[ki]) c = cells_r[q];
            }
            tui_style st = tui_fg(MT_FAINT);
            const char *t = c ? cell_text(f, c, &st) : "·   ";
            if (selp && has_sel_m && sel_m == ids[ki]) st = tui_style_add(st, TUI_REVERSED);
            tui_line_push(f->a, &l, mt_styled(MT_FMT(f, "%s ", t), st));
        }
        mt_lines_push(f, &lines, l);
    }
    if (nprog == 0) mt_lines_push(f, &lines, tui_line_span(f->a, mt_label("no programs in the desired state")));
    size_t shown = nprog < rows_fit ? nprog : rows_fit;
    const char *more = shown < nprog ? MT_FMT(f, "%zu-%zu of %zu (↑↓) ", first + 1, first + shown, nprog) : "";
    tui_block pb = mt_panel(f, MT_LINE(f, mt_fgb(" placement ", MT_FG), mt_fgs(more, MT_WARN),
                                       mt_label("● run ○ placed ◌ leaving; C/M/W/N exec; p pin >< moving r reserved ")),
                            MT_FAINT);
    par_block(f, mt_text(f, &lines), &pb, mat);
    if (side) {
        mt_lines l = {0};
        const mt_vrec *p = NULL;
        for (size_t i = 0; i < nprog && sel_pa && !p; i++)
            if (streq_opt(rget(progs[i], "name"), sel_pa)) p = progs[i];
        if (p) {
            mt_lines_push(f, &l, MT_LINE(f, mt_fgb(MT_FMT(f, "%s ", rs(p, "name")), MT_FG), mt_label("at "), mt_val(rs(p, "at")), mt_label(" pin "),
                                         mt_val(rs(p, "pin")), mt_label(" replicas "), mt_val(rs(p, "replicas")), mt_label(" migrate "),
                                         mt_val(rs(p, "migrate"))));
            if (*p->text) mt_lines_push(f, &l, tui_line_span(f->a, mt_fgs(p->text, MT_DIM)));
            const mt_vrec **ex;
            size_t nex = all(f, v, "explain", &ex);
            for (size_t i = 0; i < nex; i++) {
                if (!streq_opt(rget(ex[i], "program"), rget(p, "name"))) continue;
                uint64_t km = 0;
                ru(ex[i], "member", &km);
                uint32_t k = (uint32_t)km;
                const char *verdict = rs(ex[i], "verdict");
                mt_lines_push(f, &l, MT_LINE(f, mt_fgb(MT_FMT(f, "%u ", k), mt_sys_color(k)), mt_styled(mt_pad(f, verdict, 9, '<'), vstate_style(verdict)),
                                             mt_fgs(ex[i]->text, strcmp(verdict, "rejected") == 0 ? MT_DIM : MT_FG)));
            }
        }
        mt_lines notes = {0};
        const mt_vrec **rr;
        size_t nr = all(f, v, "proposal", &rr);
        for (size_t i = 0; i < nr; i++) mt_lines_push(f, &notes, MT_LINE(f, mt_fgb("proposal ", MT_WARN), tui_span_raw(rr[i]->text)));
        nr = all(f, v, "migration", &rr);
        for (size_t i = 0; i < nr; i++)
            mt_lines_push(f, &notes, MT_LINE(f, mt_fgb("migrating ", MT_ACCENT),
                                             mt_val(MT_FMT(f, "%s %s→%s ", rs(rr[i], "programs"), rs(rr[i], "from"), rs(rr[i], "to"))),
                                             mt_label(MT_FMT(f, "%s s", rs(rr[i], "secs")))));
        nr = all(f, v, "note", &rr);
        for (size_t i = 0; i < nr; i++) mt_lines_push(f, &notes, MT_LINE(f, mt_label("note "), tui_span_raw(rr[i]->text)));
        if (notes.n) {
            mt_lines_push(f, &l, tui_line_from_str(f->a, ""));
            for (size_t i = 0; i < notes.n; i++) mt_lines_push(f, &l, notes.l[i]);
        }
        nr = all(f, v, "decision", &rr);
        if (nr) {
            mt_lines_push(f, &l, tui_line_span(f->a, mt_fgb("decisions (newest first)", MT_FG)));
            for (size_t i = 0; i < nr; i++)
                mt_lines_push(f, &l, MT_LINE(f, mt_label(MT_FMT(f, "%ss ", mt_pad(f, rs(rr[i], "at_s"), 7, '>'))), tui_span_raw(rr[i]->text)));
        }
        tui_block wb = mt_panel(f, MT_LINE(f, mt_fgb(" why ", MT_FG), mt_label("the selected program's explain, the scheduler's decisions ")), MT_FAINT);
        par_wrap(f, mt_text(f, &l), &wb, why);
    }
    if (grp.height > 0) {
        mt_lines l = {0};
        const mt_vrec **gms;
        size_t ngm = all(f, v, "gmember", &gms);
        for (size_t gi = 0; gi < ngroups; gi++) {
            const mt_vrec *g = groups[gi];
            const char *name = rs(g, "name");
            mt_lines_push(f, &l, MT_LINE(f, mt_fgb(MT_FMT(f, "%s ", name), MT_FG), mt_label("epoch "), mt_val(rs(g, "epoch")), mt_label(" cycle "),
                                         mt_val(rs(g, "cycle")), mt_label(" members "), mt_val(rs(g, "members")), mt_label(" state "),
                                         mt_styled(rs(g, "state"), vstate_style(rs(g, "state"))), mt_label(" on loss "),
                                         mt_val(streq_opt(rget(g, "on_loss"), "reform") ? MT_FMT(f, "reform after %s ms", rs(g, "auto_ms")) : "pause")));
            tui_line per = tui_line_span(f->a, mt_label("ranks "));
            for (size_t i = 0; i < ngm; i++) {
                if (!streq_opt(rget(gms[i], "group"), name)) continue;
                uint64_t km = 0;
                ru(gms[i], "member", &km);
                uint32_t k = (uint32_t)km;
                tui_line_push(f->a, &per, mt_fgb(MT_FMT(f, "%u", k), mt_sys_color(k)));
                tui_line_push(f->a, &per, mt_label(MT_FMT(f, "#%s ", rs(gms[i], "rank"))));
                tui_line_push(f->a, &per, mt_styled(MT_FMT(f, "%s@%s  ", rs(gms[i], "state"), rs(gms[i], "epoch")), vstate_style(rs(gms[i], "state"))));
            }
            mt_lines_push(f, &l, per);
        }
        tui_block gb = mt_panel(f, tui_line_span(f->a, mt_fgb(" lockstep groups ", MT_FG)), MT_FAINT);
        par_block(f, mt_text(f, &l), &gb, grp);
    }
}

/* ---------------------------------------------------------------- 0 jobs */

static const char *rate_or_dash(mt_frame *f, const mt_vrec *r, const char *k) {
    double x;
    return rf(r, k, &x) ? mt_fmt_rate(f, (float)x) : "-";
}

void mt_draw_jobs(mt_frame *f, tui_rect area, const mt_app *app, mt_instant now) {
    const mt_view *v = mt_app_view(app, 1);
    if (!v) {
        waiting(f, area, app, 1);
        return;
    }
    const mt_vrec *head = mt_view_first(v, "jobs");
    if (!head) head = &EMPTY_REC;
    const mt_vrec **jobs;
    size_t nj = all(f, v, "job", &jobs);
    size_t active = 0;
    for (size_t i = 0; i < nj; i++) active += streq_opt(rget(jobs[i], "state"), "running") || streq_opt(rget(jobs[i], "state"), "cancelling");
    tui_line l1 = MT_LINE(f, mt_label("jobs "), mt_val(MT_FMT(f, "%zu", nj)), mt_label(" active "), mt_val(MT_FMT(f, "%zu", active)), mt_label("  leader "),
                          mt_val(rs(head, "leader")), mt_label(" ("), mt_fgs(rs(head, "role"), MT_ACCENT), mt_label(")  commit "),
                          mt_val(rs(head, "commit")), mt_label("  items/s "), mt_val(rate_or_dash(f, head, "rate")), mt_label("  leases held "),
                          mt_val(rs(head, "held")), source(f, app, v, now));
    if (area.height < 6 || area.width < 40) {
        mt_lines lines = {0};
        mt_lines_push(f, &lines, l1);
        if (nj) {
            const mt_vrec *j = jobs[0];
            double p;
            const char *prog = rf(j, "progress", &p) ? MT_FMT(f, "%s%%", mt_ff(f, p * 100.0, 0)) : "endless";
            mt_lines_push(f, &lines, MT_LINE(f, mt_val(rs(j, "job")), mt_label(" "), mt_val(rs(j, "program")), mt_label(" "),
                                             mt_styled(rs(j, "state"), vstate_style(rs(j, "state"))), mt_label(MT_FMT(f, " %s", prog))));
        }
        par_wrap(f, mt_text(f, &lines), NULL, area);
        return;
    }
    char *sel_m = mt_app_job_sel(app);
    const char *sel = sel_m ? tui_arena_strdup(f->a, sel_m) : NULL;
    free(sel_m);
    int tblh = (int)(nj ? nj : 1) + 3;
    tui_rect sp[3];
    split_v(area, (int[]){1, tblh < 12 ? tblh : 12, -1}, 3, sp);
    tui_rect hd = sp[0], tbl = sp[1], detail = sp[2];
    mt_par_line(f, l1, hd);
    bool wide = area.width >= 110, narrow = area.width < 96;
    const char *header[11];
    size_t nh = 0;
    const char *h0[6] = {"job", "program", "state", "progress", "done / items", "items/s"};
    for (size_t i = 0; i < 6; i++) header[nh++] = h0[i];
    if (!narrow) {
        header[nh++] = "ETA";
        header[nh++] = "failed";
        header[nh++] = "retried";
        header[nh++] = "dupes";
    }
    if (wide) header[nh++] = "prio";
    mt_rows rows = {0};
    for (size_t i = 0; i < nj; i++) {
        const mt_vrec *j = jobs[i];
        const char *st = rs(j, "state");
        double p;
        mt_cells cells = {0};
        mt_cells_push(f, &cells, mt_cell_span(f, mt_fgb(rs(j, "job"), MT_FG)));
        mt_cells_push(f, &cells, mt_cell_str(f, mt_cut(f, rs(j, "program"), 10)));
        mt_cells_push(f, &cells, mt_cell_span(f, mt_styled(mt_cut(f, st, 9), vstate_style(st))));
        if (rf(j, "progress", &p))
            mt_cells_push(f, &cells, mt_cell_line(f, MT_LINE(f, mt_fgs(mt_bar(f, p, 8), strcmp(st, "running") == 0 ? MT_ACCENT : MT_GOOD),
                                                               mt_label(pct3(f, p * 100.0)))));
        else
            mt_cells_push(f, &cells, mt_cell_line(f, tui_line_span(f->a, mt_fgs("endless", MT_DIM))));
        mt_cells_push(f, &cells, mt_cell_str(f, MT_FMT(f, "%s / %s", rs(j, "done"), rs(j, "items"))));
        mt_cells_push(f, &cells, mt_cell_str(f, rate_or_dash(f, j, "rate")));
        if (!narrow) {
            double eta;
            uint64_t u;
            mt_cells_push(f, &cells, mt_cell_str(f, rf(j, "eta_s", &eta) ? secs_text(f, eta) : "-"));
            mt_cells_push(f, &cells, mt_cell_span(f, mt_fgs(rs(j, "failed"), ru(j, "failed", &u) && u > 0 ? MT_BAD : MT_DIM)));
            mt_cells_push(f, &cells, mt_cell_span(f, mt_fgs(rs(j, "retried"), ru(j, "retried", &u) && u > 0 ? MT_WARN : MT_DIM)));
            mt_cells_push(f, &cells, mt_cell_span(f, mt_fgs(rs(j, "dupes"), ru(j, "dupes", &u) && u > 0 ? MT_WARN : MT_DIM)));
        }
        if (wide) mt_cells_push(f, &cells, mt_cell_str(f, rs(j, "priority")));
        tui_table_row row = mt_row(f, cells.c, cells.n);
        if (streq_opt(sel, rget(j, "job"))) row.style = tui_style_bg(tui_style_new(), MT_FAINT);
        mt_rows_push(f, &rows, row);
    }
    tui_constraint widths[11];
    size_t nw = 0;
    const uint16_t w0[6] = {5, 10, 9, 13, 17, 7};
    for (size_t i = 0; i < 6; i++) widths[nw++] = TUI_LENGTH(w0[i]);
    if (!narrow) {
        widths[nw++] = TUI_LENGTH(6);
        widths[nw++] = TUI_LENGTH(6);
        widths[nw++] = TUI_LENGTH(7);
        widths[nw++] = TUI_LENGTH(5);
    }
    if (wide) widths[nw++] = TUI_LENGTH(4);
    tui_block jb = mt_panel(f, MT_LINE(f, mt_fgb(" jobs ", MT_FG), mt_label("active and finished, as this member's committed table has them ")), MT_FAINT);
    tui_table_state ts = {0};
    for (size_t i = 0; i < nj && sel; i++)
        if (streq_opt(rget(jobs[i], "job"), sel)) {
            ts.has_selected = true;
            ts.selected = i;
            break;
        }
    draw_table(f, &rows, header, nh, widths, nw, &jb, tbl, &ts);
    if (detail.height < 3) return;
    if (!sel) {
        tui_block b = mt_panel(f, tui_line_from_str(f->a, " job "), MT_FAINT);
        par_block(f, tui_text_line(f->a, tui_line_span(f->a, mt_label("no jobs: `job submit <program> items <n>` on any member"))), &b, detail);
        return;
    }
    const mt_vrec **shares, **picks, **rr;
    size_t ns = 0, np = 0;
    {
        size_t n = all(f, v, "share", &rr);
        shares = tui_arena_alloc(f->a, (n ? n : 1) * sizeof *shares);
        for (size_t i = 0; i < n; i++)
            if (streq_opt(rget(rr[i], "job"), sel)) shares[ns++] = rr[i];
        n = all(f, v, "pick", &rr);
        picks = tui_arena_alloc(f->a, (n ? n : 1) * sizeof *picks);
        for (size_t i = 0; i < n; i++)
            if (streq_opt(rget(rr[i], "job"), sel)) picks[np++] = rr[i];
    }
    bool side = detail.width >= 90;
    tui_rect hs[2];
    tui_hsplit(detail, 2, (tui_constraint[]){TUI_PERCENTAGE(side ? 55 : 100), TUI_FILL(1)}, hs);
    /* Per member: done (its share of the job), what it holds now, its measured rate. */
    uint64_t total = 0;
    for (size_t i = 0; i < ns; i++) {
        uint64_t d;
        if (ru(shares[i], "done", &d)) total += d;
    }
    mt_rows srows = {0};
    for (size_t i = 0; i < ns; i++) {
        const mt_vrec *s = shares[i];
        uint64_t km = 0, d = 0;
        ru(s, "member", &km);
        ru(s, "done", &d);
        uint32_t k = (uint32_t)km;
        double frac = (double)d / (double)(total > 1 ? total : 1);
        const char *host = "";
        for (size_t q = 0; q < app->nmachines; q++) {
            uint32_t sk;
            if (mt_machine_system(&app->machines[q], &sk) && sk == k) {
                host = mt_name(f, &app->machines[q]);
                break;
            }
        }
        tui_table_cell c[6] = {
            mt_cell_span(f, mt_fgb(MT_FMT(f, "%u", k), mt_sys_color(k))),
            mt_cell_str(f, mt_cut(f, host, 10)),
            mt_cell_line(f, MT_LINE(f, mt_fgs(mt_bar(f, frac, 6), mt_sys_color(k)), mt_val(pct3(f, frac * 100.0)))),
            mt_cell_str(f, MT_FMT(f, "%llu", (unsigned long long)d)),
            mt_cell_str(f, rs(s, "leased")),
            mt_cell_str(f, rate_or_dash(f, s, "rate")),
        };
        mt_rows_push(f, &srows, mt_row(f, c, 6));
    }
    if (srows.n == 0) {
        tui_table_cell c = mt_cell_span(f, mt_label("no member has run an item yet"));
        mt_rows_push(f, &srows, mt_row(f, &c, 1));
    }
    uint16_t nrows = (uint16_t)srows.n;
    static const char *const sh[6] = {"sys", "host", "share", "done", "leased", "items/s"};
    static const tui_constraint sw[6] = {{TUI_C_LENGTH, 3, 0}, {TUI_C_LENGTH, 10, 0}, {TUI_C_LENGTH, 11, 0},
                                         {TUI_C_LENGTH, 8, 0}, {TUI_C_LENGTH, 7, 0},  {TUI_C_FILL, 1, 0}};
    mt_lines l = {0};
    const mt_vrec **results, **failed;
    size_t nres = 0, nfail = 0;
    {
        size_t n = all(f, v, "result", &rr);
        results = tui_arena_alloc(f->a, (n ? n : 1) * sizeof *results);
        for (size_t i = 0; i < n; i++)
            if (streq_opt(rget(rr[i], "job"), sel)) results[nres++] = rr[i];
        n = all(f, v, "failed", &rr);
        failed = tui_arena_alloc(f->a, (n ? n : 1) * sizeof *failed);
        for (size_t i = 0; i < n; i++)
            if (streq_opt(rget(rr[i], "job"), sel)) failed[nfail++] = rr[i];
    }
    if (nres || nfail) {
        mt_lines_push(f, &l, tui_line_span(f->a, mt_fgb("results (last) / failed (first)", MT_FG)));
        for (size_t i = 0; i < nres; i++)
            mt_lines_push(f, &l, MT_LINE(f, mt_label("item "), mt_val(rs(results[i], "item")), mt_label(" = "), mt_val(rs(results[i], "result")),
                                         mt_label(MT_FMT(f, " by %s ", rs(results[i], "member"))), mt_fgs(rs(results[i], "out"), MT_DIM)));
        for (size_t i = 0; i < nfail; i++)
            mt_lines_push(f, &l, MT_LINE(f, mt_fgs("failed ", MT_BAD), mt_val(rs(failed[i], "item")),
                                         mt_label(MT_FMT(f, " after %s tries: ", rs(failed[i], "tries"))), tui_span_raw(failed[i]->text)));
    }
    const mt_vrec *j = NULL;
    for (size_t i = 0; i < nj && !j; i++)
        if (streq_opt(rget(jobs[i], "job"), sel)) j = jobs[i];
    tui_block block = mt_panel(f, MT_LINE(f, mt_fgb(MT_FMT(f, " job %s ", sel), MT_FG), mt_label(MT_FMT(f, "%s members ", j ? rs(j, "program") : "-"))),
                               MT_FAINT);
    tui_rect inner = tui_block_inner(&block, hs[0]);
    mt_block(f, &block, hs[0]);
    tui_rect s2[2];
    split_v(inner, (int[]){nrows + 1, -1}, 2, s2);
    draw_table(f, &srows, sh, 6, sw, 6, NULL, s2[0], NULL);
    par_wrap(f, mt_text(f, &l), NULL, s2[1]);
    if (side) {
        mt_lines pl = {0};
        for (size_t i = 0; i < np; i++) {
            uint64_t km = 0;
            ru(picks[i], "member", &km);
            uint32_t k = (uint32_t)km;
            const char *verdict = rs(picks[i], "verdict");
            mt_lines_push(f, &pl, MT_LINE(f, mt_fgb(MT_FMT(f, "%u ", k), mt_sys_color(k)),
                                          mt_styled(mt_pad(f, mt_vrec_flag(picks[i], "busy") ? "busy" : verdict, 9, '<'), vstate_style(verdict)),
                                          mt_fgs(picks[i]->text, strcmp(verdict, "rejected") == 0 ? MT_DIM : MT_FG)));
        }
        if (np == 0) {
            bool lead = streq_opt(rget(head, "role"), "leader") || streq_opt(rget(head, "role"), "single");
            mt_lines_push(f, &pl, tui_line_span(f->a, mt_label(lead ? "no lease to choose (nothing queued)" : "the leader's view has the lease choice")));
        }
        tui_block rb = mt_panel(f, MT_LINE(f, mt_fgb(" lease choice ", MT_FG), mt_label("F4's filter and score ")), MT_FAINT);
        par_wrap(f, mt_text(f, &pl), &rb, hs[1]);
    }
}

/* ---------------------------------------------------------------- - pipes */

void mt_draw_pipes(mt_frame *f, tui_rect area, const mt_app *app, mt_instant now) {
    const mt_view *v = mt_app_view(app, 2);
    if (!v) {
        waiting(f, area, app, 2);
        return;
    }
    const mt_vrec **inboxes, **feeds;
    size_t ni = all(f, v, "inbox", &inboxes), nf = all(f, v, "feed", &feeds);
    size_t refusing = 0;
    for (size_t i = 0; i < ni; i++) {
        double x;
        refusing += rf(inboxes[i], "refused_s", &x) && x > 0.0;
    }
    tui_line l1 = MT_LINE(f, mt_label("inboxes "), mt_val(MT_FMT(f, "%zu", ni)), mt_label("  producers "), mt_val(MT_FMT(f, "%zu", nf)),
                          mt_label("  refusing now (backpressure) "),
                          mt_styled(MT_FMT(f, "%zu", refusing), refusing > 0 ? tui_fg_bold(MT_WARN) : tui_fg(MT_DIM)), source(f, app, v, now));
    if (area.height < 6 || area.width < 40) {
        par_wrap(f, tui_text_line(f->a, l1), NULL, area);
        return;
    }
    int fdh = (int)nf + 2;
    tui_rect sp[3];
    split_v(area, (int[]){1, -1, fdh < 6 ? fdh : 6}, 3, sp);
    mt_par_line(f, l1, sp[0]);
    bool wide = area.width >= 96;
    const char *header[10];
    size_t nh = 0;
    const char *h0[7] = {"consumer", "type", "depth", "in/s", "out/s", "refused/s", "refused"};
    for (size_t i = 0; i < 7; i++) header[nh++] = h0[i];
    if (wide) {
        header[nh++] = "empty";
        header[nh++] = "rec";
    }
    header[nh++] = "fed by";
    mt_rows rows = {0};
    for (size_t i = 0; i < ni; i++) {
        const mt_vrec *r = inboxes[i];
        double fill = 0.0, rsv = 0.0;
        rf(r, "fill", &fill);
        rf(r, "refused_s", &rsv);
        mt_cells cells = {0};
        mt_cells_push(f, &cells, mt_cell_span(f, mt_fgb(mt_cut(f, rs(r, "consumer"), 12), MT_FG)));
        mt_cells_push(f, &cells, mt_cell_str(f, mt_cut(f, rs(r, "type"), 10)));
        mt_cells_push(f, &cells, mt_cell_line(f, MT_LINE(f, mt_fgs(mt_bar(f, fill, 8), fill > 0.9 ? MT_BAD : fill > 0.5 ? MT_WARN : MT_GOOD),
                                                           mt_label(MT_FMT(f, " %s/%s", rs(r, "depth"), rs(r, "cap"))))));
        mt_cells_push(f, &cells, mt_cell_str(f, rate_or_dash(f, r, "in_s")));
        mt_cells_push(f, &cells, mt_cell_str(f, rate_or_dash(f, r, "out_s")));
        mt_cells_push(f, &cells, mt_cell_span(f, mt_styled(mt_fmt_rate(f, (float)rsv), rsv > 0.0 ? tui_fg_bold(MT_WARN) : tui_fg(MT_DIM))));
        mt_cells_push(f, &cells, mt_cell_str(f, rs(r, "refused")));
        if (wide) {
            mt_cells_push(f, &cells, mt_cell_str(f, rs(r, "empty")));
            mt_cells_push(f, &cells, mt_cell_str(f, MT_FMT(f, "%s B", rs(r, "rec"))));
        }
        mt_cells_push(f, &cells, mt_cell_str(f, rs(r, "fed_by")));
        mt_rows_push(f, &rows, mt_row(f, cells.c, cells.n));
    }
    tui_constraint widths[10];
    size_t nw = 0;
    const uint16_t w0[7] = {12, 10, 20, 8, 8, 9, 9};
    for (size_t i = 0; i < 7; i++) widths[nw++] = TUI_LENGTH(w0[i]);
    if (wide) {
        widths[nw++] = TUI_LENGTH(9);
        widths[nw++] = TUI_LENGTH(6);
    }
    widths[nw++] = TUI_FILL(1);
    if (ni == 0) {
        tui_block b = mt_panel(f, tui_line_from_str(f->a, " inboxes "), MT_FAINT);
        par_block(f, tui_text_line(f->a, tui_line_span(f->a, mt_label("no program with an inbox is loaded here (pipes are per machine: [ ] picks another)"))),
                  &b, sp[1]);
    } else {
        tui_block b = mt_panel(f, MT_LINE(f, mt_fgb(" inboxes ", MT_FG), mt_label("depth, rates, pushes refused while full (backpressure) ")), MT_FAINT);
        draw_table(f, &rows, header, nh, widths, nw, &b, sp[1], NULL);
    }
    if (sp[2].height > 0) {
        mt_lines l = {0};
        for (size_t i = 0; i < nf; i++)
            mt_lines_push(f, &l, MT_LINE(f, mt_val(rs(feeds[i], "producer")), mt_label(" → "), tui_span_raw(rs(feeds[i], "consumers"))));
        tui_block b = mt_panel(f, tui_line_span(f->a, mt_fgb(" feeds ", MT_FG)), MT_FAINT);
        par_block(f, mt_text(f, &l), &b, sp[2]);
    }
}

/* ---------------------------------------------------------------- fleet from the snapshots */

static tui_style rs_style(uint32_t st) {
    switch (st) {
    case MESH_RS_SYNC: return tui_fg(MT_GOOD);
    case MESH_RS_PENDING: return tui_fg(MT_WARN);
    case MESH_RS_FAILED: return tui_fg_bold(MT_BAD);
    default: return tui_fg(MT_DIM);
    }
}

static tui_style gl_style(uint32_t st) {
    switch (st) {
    case MESH_GL_RUNNING: return tui_fg(MT_GOOD);
    case MESH_GL_HELLO: return tui_fg(MT_WARN);
    case MESH_GL_PAUSED: return tui_fg(MT_BAD);
    default: return tui_fg(MT_DIM);
    }
}

static const char *gl_name(uint32_t g) { return g < MESH_GL_NAMES_LEN ? MESH_GL_NAMES[g] : "?"; }

void mt_draw_fleet_snap(mt_frame *f, tui_rect area, const mt_app *app) {
    uint16_t n = (uint16_t)app->nmachines;
    /* The member table and group come from the focused machine, or the leader's machine when the
     * focused one has no fleet. */
    bool has_pick = false;
    size_t src = 0;
    mt_fleet fl;
    const mt_machine *fm0 = mt_app_focused(app);
    if (fm0->has_snap && mt_snapshot_fleet(&fm0->snap, &fl)) {
        has_pick = true;
        src = app->focus;
    } else {
        for (size_t i = 0; i < app->nmachines && !has_pick; i++) {
            const mt_machine *m = &app->machines[i];
            if (m->has_snap && mt_snapshot_fleet(&m->snap, &fl) && fl.has_leader && fl.role == MESH_ROLE_LEADER) {
                has_pick = true;
                src = i;
            }
        }
    }
    uint16_t nm = has_pick ? (uint16_t)fl.nmembers : 0;
    tui_rect r[3];
    tui_vsplit(area, 3, (tui_constraint[]){TUI_LENGTH(tui_min16((uint16_t)(n + 3), area.height)), TUI_LENGTH(nm + 3), TUI_FILL(1)}, r);
    tui_rect views = r[0], members = r[1], group = r[2];
    /* 1. Every machine's view: role, leader, term, version committed / applied, up, majority. */
    static const char *const header[10] = {"machine", "system", "role", "leader", "term", "version", "applied", "up", "suspect", "majority"};
    mt_rows rows = {0};
    for (size_t i = 0; i < app->nmachines; i++) {
        const mt_machine *m = &app->machines[i];
        tui_table_cell name = mt_cell_span(f, mt_fgb(mt_name(f, m), MT_MACHINE[i % 8]));
        mt_fleet x;
        if (!m->has_snap) {
            tui_table_cell c[2] = {name, mt_cell_span(f, mt_fgs(m->status, MT_DIM))};
            mt_rows_push(f, &rows, mt_row(f, c, 2));
            continue;
        }
        if (!mt_snapshot_fleet(&m->snap, &x)) {
            tui_table_cell c[2] = {name, mt_cell_span(f, mt_label("single (not in a fleet)"))};
            mt_rows_push(f, &rows, mt_row(f, c, 2));
            continue;
        }
        tui_span maj = x.flags & MESH_FF_FIXED ? mt_fgs("fixed pair", MT_ACCENT) : x.flags & MESH_FF_QUORUM ? mt_fgs("yes", MT_GOOD) : mt_fgb("NO", MT_BAD);
        uint32_t k;
        bool has_k = mt_snapshot_system(&m->snap, &k);
        tui_table_cell c[10] = {
            name,
            mt_cell_str(f, has_k ? MT_FMT(f, "%u", k) : "-"),
            mt_cell_span(f, mt_styled(x.role < MESH_ROLE_NAMES_LEN ? MESH_ROLE_NAMES[x.role] : "?",
                                      x.role == MESH_ROLE_LEADER ? tui_fg_bold(MT_ACCENT) : tui_fg(MT_FG))),
            mt_cell_span(f, x.has_leader ? mt_fgb(MT_FMT(f, "%u", x.leader), mt_sys_color(x.leader)) : mt_fgb("none", MT_BAD)),
            mt_cell_str(f, MT_FMT(f, "%llu", (unsigned long long)x.term)),
            mt_cell_str(f, MT_FMT(f, "%llu", (unsigned long long)x.version)),
            mt_cell_span(f, mt_fgs(MT_FMT(f, "%llu", (unsigned long long)x.applied), x.applied == x.version ? MT_GOOD : MT_WARN)),
            mt_cell_str(f, mt_mask_list(f, x.up)),
            mt_cell_span(f, mt_fgs(mt_mask_list(f, x.suspect), x.suspect != 0 ? MT_WARN : MT_DIM)),
            mt_cell_span(f, maj),
        };
        mt_rows_push(f, &rows, mt_row(f, c, 10));
    }
    static const tui_constraint w[10] = {{TUI_C_LENGTH, 14, 0}, {TUI_C_LENGTH, 6, 0}, {TUI_C_LENGTH, 13, 0}, {TUI_C_LENGTH, 6, 0},
                                         {TUI_C_LENGTH, 5, 0},  {TUI_C_LENGTH, 7, 0}, {TUI_C_LENGTH, 7, 0},  {TUI_C_LENGTH, 15, 0},
                                         {TUI_C_LENGTH, 9, 0},  {TUI_C_FILL, 1, 0}};
    tui_block vb = mt_panel(f, MT_LINE(f, mt_fgb(" fleet ", MT_FG), mt_label("each member's view: the leader, its term, the desired state committed and applied ")),
                            MT_FAINT);
    draw_table(f, &rows, header, 10, w, 10, &vb, views, NULL);
    if (!has_pick) {
        tui_block b = mt_panel(f, tui_line_from_str(f->a, " members "), MT_FAINT);
        par_block(f, tui_text_line(f->a, tui_line_span(f->a, mt_label("no machine is in a fleet"))), &b, members);
        return;
    }
    const mt_snapshot *s = &app->machines[src].snap;
    uint64_t now = mt_snapshot_uptime_ms(s);
    /* 2. The members as machine src sees them. */
    if (members.height > 0) {
        static const char *const mh[9] = {"system", "host", "link", "", "reconcile", "applied", "heard", "group", "reason / last"};
        mt_rows mrows = {0};
        uint32_t me;
        bool has_me = mt_snapshot_system(s, &me);
        for (size_t ki = 0; ki < fl.nmembers; ki++) {
            uint32_t k = (uint32_t)ki;
            const mt_fleet_member *fm = &fl.members[ki];
            mt_member mr = mt_snapshot_member(s, k);
            const char *link = mr.state < MESH_MEMBER_STATES_LEN ? MESH_MEMBER_STATES[mr.state] : "?";
            tui_style link_style = mr.state == MESH_MS_UP        ? tui_fg(MT_GOOD)
                                   : mr.state == MESH_MS_SUSPECT ? tui_fg_bold(MT_WARN)
                                   : mr.state == MESH_MS_LINKED  ? tui_fg(MT_WARN)
                                                                 : tui_fg_bold(MT_BAD);
            bool pending = fm->applied < fl.version && fm->status != MESH_RS_FAILED;
            const char *rsw = pending ? "pending" : fm->status < MESH_RS_NAMES_LEN ? MESH_RS_NAMES[fm->status] : "?";
            tui_style rss = strcmp(rsw, "pending") == 0 ? tui_fg(MT_WARN) : rs_style(fm->status);
            const char *heard = has_me && me == k   ? "-"
                                : fm->heard_ms == 0 ? "never"
                                                    : MT_FMT(f, "%llu ms", (unsigned long long)(now > fm->heard_ms ? now - fm->heard_ms : 0));
            tui_table_cell c[9] = {
                mt_cell_span(f, mt_fgb(MT_FMT(f, "%u", k), mt_sys_color(k))),
                mt_cell_str(f, tui_arena_strdup(f->a, mr.host)),
                mt_cell_span(f, mt_styled(link, link_style)),
                mt_cell_span(f, mt_fgb(fl.has_leader && fl.leader == k ? "leader" : "", MT_ACCENT)),
                mt_cell_span(f, mt_styled(rsw, rss)),
                mt_cell_str(f, MT_FMT(f, "%llu", (unsigned long long)fm->applied)),
                mt_cell_str(f, heard),
                mt_cell_span(f, mt_styled(gl_name(fm->group), gl_style(fm->group))),
                mt_cell_span(f, mt_fgs(tui_arena_strdup(f->a, fm->reason), fm->status == MESH_RS_FAILED ? MT_BAD : MT_DIM)),
            };
            mt_rows_push(f, &mrows, mt_row(f, c, 9));
        }
        static const tui_constraint mw[9] = {{TUI_C_LENGTH, 6, 0}, {TUI_C_LENGTH, 16, 0}, {TUI_C_LENGTH, 8, 0}, {TUI_C_LENGTH, 6, 0}, {TUI_C_LENGTH, 9, 0},
                                             {TUI_C_LENGTH, 7, 0}, {TUI_C_LENGTH, 8, 0},  {TUI_C_LENGTH, 8, 0}, {TUI_C_FILL, 1, 0}};
        tui_block mb = mt_panel(f, MT_LINE(f, mt_fgb(MT_FMT(f, " members as %s sees them ", mt_name(f, &app->machines[src])), MT_FG),
                                           mt_label(MT_FMT(f, "dead after %u ms; majority %u ", fl.dead_ms, fl.quorum))),
                                MT_FAINT);
        draw_table(f, &mrows, mh, 9, mw, 9, &mb, members, NULL);
    }
    /* 3. The lockstep group and the jobs. */
    if (group.height > 0) {
        mt_lines lines = {0};
        if (fl.has_group) {
            const mt_fleet_group *g = &fl.group;
            const char *order = "";
            for (size_t i = 0; i < MESH_MAX_SYSTEMS && g->order[i] != 0xff; i++) order = MT_FMT(f, "%s%s%u", order, i ? "," : "", g->order[i]);
            const char *loss = g->reform ? (g->auto_ms > 0 ? MT_FMT(f, "reform after %u ms", g->auto_ms) : "reform (manual)") : "pause";
            mt_lines_push(f, &lines, MT_LINE(f, mt_fgb(MT_FMT(f, "%s ", g->name), MT_FG), mt_label("epoch "), mt_val(MT_FMT(f, "%u", g->epoch)),
                                             mt_label("  cycle "), mt_val(MT_FMT(f, "%u", g->cycle)), mt_label("  members (rank order) "), mt_val(order),
                                             mt_label("  state "), mt_styled(g->running ? "running" : "paused", g->running ? tui_fg(MT_GOOD) : tui_fg_bold(MT_BAD)),
                                             mt_label("  on loss "), mt_val(loss)));
            tui_line per = tui_line_span(f->a, mt_label("data plane: "));
            const char *out = "";
            for (size_t k = 0; k < fl.nmembers; k++) {
                if (!(g->mask >> k & 1)) {
                    out = MT_FMT(f, "%s%s%zu", out, *out ? "," : "", k);
                    continue;
                }
                tui_line_push(f->a, &per, mt_fgb(MT_FMT(f, "%zu ", k), mt_sys_color((uint32_t)k)));
                tui_line_push(f->a, &per, mt_styled(MT_FMT(f, "%s  ", gl_name(fl.members[k].group)), gl_style(fl.members[k].group)));
            }
            mt_lines_push(f, &lines, per);
            if (*out) mt_lines_push(f, &lines, MT_LINE(f, mt_label("not in this epoch (unfed until the next reform): "), mt_val(out)));
        } else {
            mt_lines_push(f, &lines, tui_line_span(f->a, mt_label("no lockstep group in the desired state")));
        }
        /* The jobs as this machine's replica has them. */
        mt_jobs j = mt_snapshot_jobs(s);
        bool lead = j.flags & MESH_JF_LEADER;
        mt_lines_push(f, &lines,
                      MT_LINE(f, mt_label("jobs: "), mt_val(MT_FMT(f, "%u active of %u", j.active, j.total)), mt_label("  items done "),
                              mt_val(MT_FMT(f, "%llu", (unsigned long long)j.done)), mt_label("  failed "),
                              mt_fgs(MT_FMT(f, "%llu", (unsigned long long)j.failed), j.failed > 0 ? MT_BAD : MT_FG), mt_label("  dupes dropped "),
                              mt_val(MT_FMT(f, "%llu", (unsigned long long)j.dupes)), mt_label(lead ? "  leased " : "  held here "),
                              mt_val(MT_FMT(f, "%u", j.leased)), mt_label("  items/s "), mt_val(mt_ff(f, j.rate, 0)),
                              mt_label(lead ? (j.flags & MESH_JF_ACTIVE ? "  (leads the job table)" : "  (leader: merging the tables)") : "")));
        tui_block gb = mt_panel(f, tui_line_span(f->a, mt_fgb(" lockstep group and jobs ", MT_FG)), MT_FAINT);
        par_block(f, mt_text(f, &lines), &gb, group);
    }
}
