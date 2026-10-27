/* ui.rs's "2 programs" section, ported. */
#include "mesh/top_ui_programs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    uint64_t *h;
    size_t n, cap;
} name_hashes;

typedef struct {
    name_hashes *v;
    size_t n, cap;
} hash_map;

static void hashes_add(mt_frame *f, hash_map *m, const char *name, uint64_t h) {
    name_hashes *e = NULL;
    for (size_t i = 0; i < m->n && !e; i++)
        if (strcmp(m->v[i].name, name) == 0) e = &m->v[i];
    if (!e) {
        if (m->n == m->cap) {
            size_t cap = m->cap ? m->cap * 2 : 8;
            m->v = tui_arena_grow(f->a, m->v, m->cap * sizeof *m->v, cap * sizeof *m->v);
            m->cap = cap;
        }
        e = &m->v[m->n++];
        memset(e, 0, sizeof *e);
        e->name = name;
    }
    if (e->n == e->cap) {
        size_t cap = e->cap ? e->cap * 2 : 4;
        e->h = tui_arena_grow(f->a, e->h, e->cap * sizeof *e->h, cap * sizeof *e->h);
        e->cap = cap;
    }
    e->h[e->n++] = h;
}

tui_table_cell mt_place_cell(mt_frame *f, const mt_section *p, uint32_t sched) {
    mt_place pl;
    if (!mt_place_of(p, &pl)) {
        if (!(sched & MESH_PS_KNOWN)) return mt_cell_styled(f, "-", tui_fg(MT_FAINT));
        const char *at;
        if (sched & MESH_PS_ALL) {
            at = "all";
        } else {
            char b[32];
            size_t n = 0;
            b[0] = 0;
            for (uint32_t k = 0; k < 8; k++)
                if (sched >> k & 1) n += (size_t)snprintf(b + n, sizeof b - n, "%s%u", n ? "," : "", k);
            at = n ? tui_arena_strdup(f->a, b) : "none";
        }
        if (sched & MESH_PS_MIGRATING) {
            uint32_t from = sched >> MESH_PS_FROM_SHIFT & 0xff;
            return mt_cell_styled(f, MT_FMT(f, "%u>%s", from ? from - 1 : 0, at), tui_fg_bold(MT_WARN));
        }
        if (sched & MESH_PS_PINNED) return mt_cell_styled(f, MT_FMT(f, "pin %s", at), tui_fg_bold(MT_ACCENT));
        return mt_cell_styled(f, at, tui_fg(MT_DIM));
    }
    switch (pl.kind) {
    case MT_PLACE_LOCAL: return mt_cell_styled(f, "local", tui_fg(MT_DIM));
    case MT_PLACE_PIN: return mt_cell_styled(f, MT_FMT(f, "pin %u", pl.pin), tui_fg_bold(MT_MACHINE[pl.pin % 8]));
    default: return mt_cell_styled(f, "split", tui_fg_bold(MT_ACCENT));
    }
}

static const char *quantile(mt_frame *f, const mt_rec *recs, size_t n, double q) {
    double v;
    return mt_quantile_us(recs, n, q, &v) ? mt_ff(f, v, 1) : "-";
}

static void draw_prog_table(mt_frame *f, tui_rect area, const mt_app *app, const mt_machine *m, bool focused, const hash_map *hashes,
                            mt_instant now) {
    /* The CPU executors: native worker processes and wasm lanes, their CPU and memory. */
    const char *workers = "";
    if (m->has_snap) {
        const mt_snapshot *s = &m->snap;
        uint32_t wk = mt_snapshot_host_u32(s, MESH_S_WORKERS), wl = mt_snapshot_host_u32(s, MESH_S_WASM_LANES);
        if (wk + wl > 0)
            workers = MT_FMT(f, "%s  workers %u, wasm lanes %u: %s%% CPU, %s", m->endpoint, wk, wl,
                             mt_ff(f, 100.0f * mt_snapshot_host_f32(s, MESH_S_EXEC_CPU), 0), mt_fmt_bytes(f, mt_snapshot_host_u64(s, MESH_S_EXEC_RSS)));
    }
    tui_color color;
    tui_line title = mt_machine_title(f, m, focused, *workers ? workers : m->endpoint, &color);
    tui_block block = mt_panel(f, title, color);
    tui_rect inner = tui_block_inner(&block, area);
    mt_block(f, &block, area);
    if (!m->has_snap) {
        mt_no_data(f, inner, m);
        return;
    }
    const mt_snapshot *s = &m->snap;
    if (s->nsections == 0) {
        mt_par_line(f, tui_line_span(f->a, mt_label("no programs loaded: press L")), inner);
        return;
    }
    mt_rec *fresh;
    size_t nfresh = mt_machine_fresh_trace(m, now, &fresh);
    /* Narrow panes drop the last columns rather than squeezing the hash. */
    size_t cols = inner.width >= 150 ? 18 : inner.width >= 120 ? 14 : 12;
    static const char *const hnames[18] = {"slot", "name", "kind", "state", "place", "nodes", "tasks/s", "p50 µs", "p95 µs",
                                           "regs/pid", "cpu%", "rss", "view B", "ptx", "warps", "rst/cr", "loads", "policy"};
    tui_table_row header = tui_table_row_new();
    for (size_t i = 0; i < cols; i++) tui_table_row_push(f->a, &header, mt_cell_styled(f, hnames[i], tui_fg(MT_DIM)));
    mt_rows rows = {0};
    mt_rec *recs = malloc((nfresh ? nfresh : 1) * sizeof *recs);
    if (!recs) abort();
    for (size_t pi = 0; pi < s->nsections; pi++) {
        const mt_section *p = &s->sections[pi];
        const char *name = mt_sec_name(f, p);
        uint32_t slot = mt_section_slot(p);
        size_t nr = 0;
        for (size_t i = 0; i < nfresh; i++)
            if (fresh[i].slot == slot) recs[nr++] = fresh[i];
        uint64_t hash = mt_section_ptx_hash(p);
        bool differs = false;
        for (size_t i = 0; i < hashes->n; i++)
            if (strcmp(hashes->v[i].name, name) == 0)
                for (size_t k = 0; k < hashes->v[i].n; k++) differs |= hashes->v[i].h[k] != hash;
        /* A GPU node's warps and, if any, its shared bytes; the CPU executors ignore the shape. */
        uint32_t shared = mt_get_u32(mt_section_ext(p), MESH_PX_BYTES, MESH_PX_SHARED);
        uint32_t warps = mt_section_pt(p, MESH_PT_WARPS);
        const char *sms = shared == 0 ? MT_FMT(f, "%u", warps) : MT_FMT(f, "%u +%uK", warps, shared / 1024 + (shared % 1024 != 0));
        uint32_t fault = mt_section_p(p, MESH_P_FAULT);
        tui_table_cell state = fault != 0 ? mt_cell_styled(f, MT_FMT(f, "%s f%u", mt_section_state_name(p), fault & 0xff), tui_fg_bold(MT_BAD))
                                          : mt_cell_styled(f, mt_section_state_name(p), mt_state_style(p));
        bool native = mt_section_native(p);
        tui_table_cell dash = mt_cell_styled(f, "-", tui_fg(MT_FAINT));
        uint32_t exec = mt_get_u32(mt_section_ext(p), MESH_PX_BYTES, MESH_PX_EXEC);
        tui_table_cell kind;
        if (exec == MESH_BACKEND_NATIVE || exec == MESH_BACKEND_WASM) {
            kind = mt_cell_styled(f, MESH_BACKENDS[exec], tui_fg(MT_ACCENT));
        } else {
            uint32_t bk = mesh_backend_kind(mt_snapshot_head(s, MESH_H_BACKEND));
            kind = mt_cell_styled(f, bk < 4 ? MESH_BACKENDS[bk] : "?", tui_fg(MT_DIM));
        }
        /* CPU executors: CPU % of one core and resident bytes (pload PL_HOST). */
        size_t pl = mesh_pload(slot);
        bool host = (mt_snapshot_host_u32(s, pl + MESH_PL_FLAGS) & MESH_PLF_HOST) != 0;
        uint32_t pid = mt_section_p(p, MESH_P_PID), cr = mt_section_p(p, MESH_P_CRASHES), spawns = mt_section_p(p, MESH_P_SPAWNS);
        uint32_t pol = mt_section_pt(p, MESH_PT_POLICY);
        tui_table_cell c[18] = {
            mt_cell_str(f, MT_FMT(f, "%u", slot)),
            mt_cell_span(f, mt_fgb(name, mt_prog_color(name, slot, 1.0f))),
            kind,
            state,
            mt_place_cell(f, p, mt_snapshot_host_u32(s, mesh_placed(slot))),
            mt_cell_str(f, MT_FMT(f, "%u/%u", mt_section_p(p, MESH_P_ONLINE), mt_section_pt(p, MESH_PT_NODES))),
            mt_cell_styled(f, mt_fmt_rate(f, mt_snapshot_rate(s, slot)), tui_fg(MT_WARN)),
            mt_cell_str(f, quantile(f, recs, nr, 0.5)),
            mt_cell_str(f, quantile(f, recs, nr, 0.95)),
            native ? (pid == 0 ? dash : mt_cell_str(f, MT_FMT(f, "%u", pid))) : mt_cell_str(f, MT_FMT(f, "%u", mt_section_pt(p, MESH_PT_REGS))),
            host ? mt_cell_str(f, mt_ff(f, 100.0f * mt_snapshot_host_f32(s, pl + MESH_PL_CPU), 0)) : dash,
            host ? mt_cell_str(f, mt_fmt_bytes(f, mt_snapshot_host_u64(s, pl + MESH_PL_RSS))) : dash,
            mt_cell_str(f, MT_FMT(f, "%u", mt_section_pt(p, MESH_PT_VIEW_BYTES))),
            mt_cell_styled(f, MT_FMT(f, "%08llx", (unsigned long long)(hash >> 32)), tui_fg(differs ? MT_WARN : MT_DIM)),
            native || exec == MESH_BACKEND_WASM ? dash : mt_cell_str(f, sms),
            native ? mt_cell_styled(f, MT_FMT(f, "%u/%u", spawns ? spawns - 1 : 0, cr), tui_fg(cr > 0 ? MT_BAD : MT_DIM)) : dash,
            mt_cell_str(f, MT_FMT(f, "%u", mt_section_pt(p, MESH_PT_LOADS))),
            mt_cell_str(f, pol < 4 ? MESH_POLICIES[pol] : "?"),
        };
        mt_rows_push(f, &rows, mt_row(f, c, cols));
    }
    free(recs);
    free(fresh);
    static const uint16_t wv[18] = {4, 15, 6, 9, 7, 5, 8, 7, 7, 8, 5, 7, 7, 8, 6, 6, 5, 10};
    tui_constraint widths[18];
    for (size_t i = 0; i < cols; i++) widths[i] = TUI_LENGTH(wv[i]);
    tui_table t = tui_table_new(rows.rows, rows.n, widths, cols);
    t.header = &header;
    t.column_spacing = 1;
    t.row_highlight_style = tui_style_add(tui_style_bg(tui_style_new(), TUI_RGB(40, 52, 80)), TUI_BOLD);
    t.highlight_symbol = tui_text_from_str(f->a, "▶");
    tui_table_state st = {0};
    if (focused) {
        st.has_selected = true;
        st.selected = app->sel < s->nsections - 1 ? app->sel : s->nsections - 1;
    }
    tui_table_render(f->a, &t, inner, f->buf, &st);
}

/* The selected program's counters, manifest fields and a hexdump of its snap bytes. */
static void draw_view_pane(mt_frame *f, tui_rect area, const mt_app *app) {
    const mt_machine *m = mt_app_focused(app);
    const mt_section *p = mt_app_selected(app);
    if (!p) {
        tui_block b = mt_panel(f, tui_line_span(f->a, mt_label(" view ")), MT_FAINT);
        tui_rect inner = tui_block_inner(&b, area);
        mt_block(f, &b, area);
        mt_par_line(f, tui_line_span(f->a, mt_label("no program selected")), inner);
        return;
    }
    const char *name = mt_sec_name(f, p);
    uint32_t slot = mt_section_slot(p);
    size_t vlen = mt_section_view_len(p);
    tui_line title = MT_LINE(f, mt_label(" view "), mt_fgb(name, mt_prog_color(name, slot, 1.0f)),
                             mt_label(MT_FMT(f, " %zu of %u B ", vlen, mt_section_pt(p, MESH_PT_VIEW_BYTES))));
    tui_block b = mt_panel(f, title, MT_FAINT);
    tui_rect inner = tui_block_inner(&b, area);
    mt_block(f, &b, area);
    mt_lines lines = {0};
    uint32_t fault = mt_section_p(p, MESH_P_FAULT);
    if (fault != 0)
        mt_lines_push(f, &lines,
                      tui_line_span(f->a, mt_styled(MT_FMT(f, " fault %u detail 0x%x ", fault & 0xff, fault >> 8), mt_fg_bg_bold(TUI_WHITE, MT_BAD))));
    tui_line cs = tui_line_empty();
    for (size_t i = 0; i < MESH_COUNTERS; i++) {
        char cn[3 * MESH_NAME_BYTES + 1];
        mt_section_counter_name(p, i, cn);
        if (!*cn) continue;
        tui_line_push(f->a, &cs, mt_label(MT_FMT(f, "%s ", cn)));
        tui_line_push(f->a, &cs, mt_val(MT_FMT(f, "%u", mt_section_counter(p, i))));
        tui_line_push(f->a, &cs, mt_label(MT_FMT(f, " (%s/s)  ", mt_fmt_rate(f, mt_machine_counter_rate(m, slot, i)))));
    }
    tui_line_push(f->a, &cs, mt_label("done "));
    tui_line_push(f->a, &cs, mt_val(MT_FMT(f, "%u", mt_section_p(p, MESH_P_DONE))));
    mt_lines_push(f, &lines, cs);
    mt_field fds[MESH_MAX_FIELDS];
    size_t nf = mt_section_fields(p, fds);
    for (size_t i = 0; i < nf; i++) {
        const char *ty = fds[i].ty < 5 ? MESH_FIELD_TYPES[fds[i].ty] : "?";
        char v[128];
        mt_section_field_value(p, &fds[i], v, sizeof v);
        mt_lines_push(f, &lines,
                      MT_LINE(f, mt_fgs(mt_pad(f, fds[i].name, 19, '<'), MT_ACCENT),
                              mt_label(MT_FMT(f, " %s @0x%04zx ", mt_pad(f, ty, 3, '<'), fds[i].off)), mt_val(tui_arena_strdup(f->a, v))));
    }
    mt_lines_push(f, &lines, tui_line_span(f->a, mt_label("── snap bytes ──")));
    size_t left = inner.height > lines.n ? inner.height - lines.n : 0;
    /* 16 bytes a row when the pane is wide enough for hex + ascii, else 8. */
    size_t per = inner.width >= 5 + 48 + 16 ? 16 : 8;
    const uint8_t *v = mt_section_view(p);
    for (size_t row = 0; row * per < vlen && row < left; row++) {
        size_t n = vlen - row * per < per ? vlen - row * per : per;
        char hex[16 * 3 + 1], ascii[17];
        for (size_t i = 0; i < n; i++) {
            uint8_t c = v[row * per + i];
            snprintf(hex + 3 * i, 4, "%02x ", c);
            ascii[i] = c > 0x20 && c < 0x7f ? (char)c : '.';
        }
        hex[3 * n] = 0;
        ascii[n] = 0;
        mt_lines_push(f, &lines,
                      MT_LINE(f, mt_label(MT_FMT(f, "%04zx ", row * per)), mt_fgs(mt_pad(f, hex, (int)(3 * per), '<'), MT_FG),
                              mt_fgs(tui_arena_strdup(f->a, ascii), MT_DIM)));
    }
    mt_par(f, mt_text(f, &lines), inner);
}

typedef struct {
    uint32_t slot, node;
    float *lane;
} lane_t;

static int lane_cmp(const void *a, const void *b) {
    const lane_t *x = a, *y = b;
    if (x->slot != y->slot) return x->slot < y->slot ? -1 : 1;
    return x->node < y->node ? -1 : x->node > y->node;
}

/* Per (program, node) lanes over the last MT_FLAME_NS of trace: each cell's glyph height is the
 * busy fraction of its time slice, its colour the program. */
static void draw_flame(mt_frame *f, tui_rect area, const mt_machine *m, mt_instant now) {
    mt_flame_rec *recs;
    size_t nrecs = mt_machine_flame(m, now, MT_FLAME_NS, &recs);
    tui_line title = MT_LINE(f, mt_label(" trace "), mt_label(MT_FMT(f, "last %u ms, %zu tasks ", MT_FLAME_NS / 1000000, nrecs)));
    tui_block b = mt_panel(f, title, MT_FAINT);
    tui_rect inner = tui_block_inner(&b, area);
    mt_block(f, &b, area);
    if (m->trace_err) {
        tui_paragraph p = tui_paragraph_new(tui_text_line(f->a, tui_line_span(f->a, mt_fgs(MT_FMT(f, "trace: %s", m->trace_err), MT_BAD))));
        p.wrap = TUI_WRAP_TRIM;
        tui_paragraph_render(f->a, &p, inner, f->buf);
        free(recs);
        return;
    }
    enum { LABEL_W = 11 };
    if (nrecs == 0 || inner.width <= LABEL_W + 2 || inner.height < 2) {
        mt_par_line(f, tui_line_span(f->a, mt_label("no recent tasks")), inner);
        free(recs);
        return;
    }
    size_t w = (size_t)(inner.width - LABEL_W);
    double cell_ns = (double)MT_FLAME_NS / (double)w;
    lane_t *lanes = tui_arena_alloc(f->a, nrecs * sizeof *lanes);
    size_t nl = 0;
    for (size_t i = 0; i < nrecs; i++) {
        const mt_rec *r = &recs[i].rec;
        double end_ago = (double)recs[i].age;
        double start_ago = end_ago + (double)mt_rec_ns(r);
        if (start_ago > (double)MT_FLAME_NS) start_ago = (double)MT_FLAME_NS;
        lane_t *lane = NULL;
        for (size_t k = 0; k < nl && !lane; k++)
            if (lanes[k].slot == r->slot && lanes[k].node == r->node) lane = &lanes[k];
        if (!lane) {
            lane = &lanes[nl++];
            lane->slot = r->slot;
            lane->node = r->node;
            lane->lane = tui_arena_alloc(f->a, w * sizeof(float));
        }
        /* Time t ago is at x = (FLAME_NS - t) / cell_ns; each cell sums its busy fraction. */
        double x0 = ((double)MT_FLAME_NS - start_ago) / cell_ns, x1 = ((double)MT_FLAME_NS - end_ago) / cell_ns;
        size_t x = (size_t)mt_sat_u64(floor(x0));
        while ((double)x < x1 && x < w) {
            lane->lane[x] += (float)fmax(fmin(x1, (double)x + 1.0) - fmax(x0, (double)x), 0.0);
            x++;
        }
    }
    free(recs);
    qsort(lanes, nl, sizeof *lanes, lane_cmp);
    static const char *const LEVELS[8] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    size_t rows = (size_t)(inner.height - 1);
    /* Lanes that do not fit leave their last row to a "+N lanes" note. */
    size_t shown = nl > rows ? rows - 1 : rows;
    for (size_t y = 0; y < nl && y < shown; y++) {
        const mt_section *sec = m->has_snap ? mt_snapshot_by_slot(&m->snap, lanes[y].slot) : NULL;
        const char *name = sec ? mt_sec_name(f, sec) : MT_FMT(f, "#%u", lanes[y].slot);
        tui_color c = mt_prog_color(name, lanes[y].slot, 1.0f);
        uint16_t yy = (uint16_t)(inner.y + y);
        const char *lab = MT_FMT(f, "%s/%s", mt_pad(f, name, 8, '>'), mt_pad(f, MT_FMT(f, "%u", lanes[y].node), 2, '<'));
        tui_buffer_set_stringn(f->buf, inner.x, yy, lab, strlen(lab), LABEL_W, tui_fg(c));
        for (size_t x = 0; x < w; x++) {
            tui_cell *cell = tui_buffer_cell(f->buf, (uint16_t)(inner.x + LABEL_W + x), yy);
            if (!cell) continue;
            float busy = lanes[y].lane[x];
            if (busy > 0.0f) {
                float bm = busy < 1.0f ? busy : 1.0f;
                size_t lvl = (size_t)mt_sat_u64(ceilf(bm * 8.0f));
                if (lvl < 1) lvl = 1;
                if (lvl > 8) lvl = 8;
                tui_cell_set_str(cell, LEVELS[lvl - 1]);
                cell->fg = c;
            } else {
                tui_cell_set_str(cell, "·");
                cell->fg = MT_FAINT;
            }
        }
    }
    if (shown < nl) {
        const char *more = MT_FMT(f, "+%zu more lanes (taller pane shows them)", nl - shown);
        tui_buffer_set_stringn(f->buf, inner.x, (uint16_t)(inner.y + shown), more, strlen(more), inner.width, tui_fg(MT_WARN));
    }
    const char *axis = mt_pad(f, MT_FMT(f, "-%ums", MT_FLAME_NS / 1000000), (int)w, '<');
    uint16_t bottom = (uint16_t)(inner.y + inner.height - 1);
    tui_buffer_set_stringn(f->buf, (uint16_t)(inner.x + LABEL_W), bottom, axis, strlen(axis), w, tui_fg(MT_DIM));
    tui_buffer_set_string(f->buf, (uint16_t)(inner.x + inner.width - 3), bottom, "now", tui_fg(MT_DIM));
}

static void draw_log(mt_frame *f, tui_rect area, const mt_machine *m) {
    tui_block b = mt_panel(f, tui_line_span(f->a, mt_label(MT_FMT(f, " log  %zu lines ", m->nlog))), MT_FAINT);
    tui_rect inner = tui_block_inner(&b, area);
    mt_block(f, &b, area);
    if (m->log_err) {
        mt_par_line(f, tui_line_span(f->a, mt_fgs(MT_FMT(f, "log: %s", m->log_err), MT_BAD)), inner);
        return;
    }
    mt_lines lines = {0};
    size_t from = m->nlog > inner.height ? m->nlog - inner.height : 0;
    for (size_t i = from; i < m->nlog; i++) {
        const char *l = m->log[i];
        tui_color c = strstr(l, "ERROR") || strstr(l, " err") ? MT_BAD : strstr(l, "WARN") ? MT_WARN : MT_DIM;
        mt_lines_push(f, &lines, tui_line_span(f->a, mt_fgs(l, c)));
    }
    mt_par(f, mt_text(f, &lines), inner);
}

void mt_draw_programs(mt_frame *f, tui_rect area, const mt_app *app, mt_instant now) {
    /* Tables get what their rows need (a third of the height at most each); the selected
     * program's view, the flame chart and the log share the rest. */
    size_t n = app->nmachines;
    uint16_t cap = tui_max16((uint16_t)(area.height / (n + 1)), 4);
    tui_constraint *cons = tui_arena_alloc(f->a, (n + 1) * sizeof *cons);
    for (size_t i = 0; i < n; i++) {
        const mt_machine *m = &app->machines[i];
        size_t secs = m->has_snap ? (m->snap.nsections ? m->snap.nsections : 1) : 1;
        cons[i] = TUI_LENGTH(tui_min16((uint16_t)(secs + 3), cap));
    }
    cons[n] = TUI_FILL(1);
    tui_rect *rows = tui_arena_alloc(f->a, (n + 1) * sizeof *rows);
    tui_vsplit(area, n + 1, cons, rows);
    /* Same name, different PTX on the two machines: shown, since peer = none programs may differ. */
    hash_map hashes = {0};
    for (size_t i = 0; i < n; i++) {
        const mt_machine *m = &app->machines[i];
        if (!m->has_snap) continue;
        for (size_t k = 0; k < m->snap.nsections; k++)
            hashes_add(f, &hashes, mt_sec_name(f, &m->snap.sections[k]), mt_section_ptx_hash(&m->snap.sections[k]));
    }
    for (size_t i = 0; i < n; i++) draw_prog_table(f, rows[i], app, &app->machines[i], i == app->focus, &hashes, now);
    tui_rect lr[2], fr[2];
    tui_hsplit(rows[n], 2, (tui_constraint[]){TUI_PERCENTAGE(45), TUI_FILL(1)}, lr);
    tui_vsplit(lr[1], 2, (tui_constraint[]){TUI_PERCENTAGE(55), TUI_FILL(1)}, fr);
    draw_view_pane(f, lr[0], app);
    draw_flame(f, fr[0], mt_app_focused(app), now);
    draw_log(f, fr[1], mt_app_focused(app));
}
