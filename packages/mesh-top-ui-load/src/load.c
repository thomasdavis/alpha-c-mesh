/* ui.rs's "8 load" section, ported. */
#include "mesh/top_ui_load.h"

#include "tui/sparkline.h"

#include <string.h>

static mt_opt_f32 some(float v) { return (mt_opt_f32){true, v}; }
static const mt_opt_f32 NONE = {false, 0.0f};

mt_load_row mt_load_row_from_load(const mt_load *l) {
    mt_load_row r = {0};
    r.share = l->share10;
    r.cpu = l->cpu;
    r.big = l->cpu_big;
    r.little = l->cpus_little > 0 ? some(l->cpu_little) : NONE;
    r.has_psi = l->has_psi;
    if (l->has_psi) {
        r.psi[0] = l->psi[0];
        r.psi[1] = l->psi[1];
    }
    r.mem_free = some(l->mem_free);
    r.meshd = l->meshd_cpu;
    return r;
}

mt_load_row mt_load_row_from_member(const mt_member_load *l) {
    mt_load_row r = {0};
    r.share = l->share10;
    r.cpu = l->cpu;
    r.big = l->cpu_big;
    r.little = l->cpus_little > 0 ? some(l->cpu_little) : NONE;
    r.has_psi = l->psi_cpu.some && l->psi_mem.some;
    if (r.has_psi) {
        r.psi[0] = l->psi_cpu.v;
        r.psi[1] = l->psi_mem.v;
    }
    r.mem_free = l->mem_total_mb > 0 ? some((float)l->mem_avail_mb / (float)l->mem_total_mb) : NONE;
    r.meshd = l->meshd_cpu;
    return r;
}

size_t mt_load_cols_header(mt_load_cols lt, const char **out) {
    switch (lt) {
    case MT_LOAD_COLS_NONE: return 0;
    case MT_LOAD_COLS_ONE: out[0] = "GPU CPU"; return 1;
    case MT_LOAD_COLS_PAIR: out[0] = "GPU CPU"; out[1] = "meshd"; return 2;
    default:
        out[0] = "GPU got";
        out[1] = "CPU B/L";
        out[2] = "PSI c/m";
        out[3] = "meshd";
        return 4;
    }
}

size_t mt_load_cols_widths(mt_load_cols lt, tui_constraint *out) {
    switch (lt) {
    case MT_LOAD_COLS_NONE: return 0;
    case MT_LOAD_COLS_ONE: out[0] = TUI_LENGTH(8); return 1;
    case MT_LOAD_COLS_PAIR: out[0] = TUI_LENGTH(8); out[1] = TUI_LENGTH(5); return 2;
    default:
        out[0] = TUI_LENGTH(7);
        out[1] = TUI_LENGTH(11);
        out[2] = TUI_LENGTH(9);
        out[3] = TUI_LENGTH(5);
        return 4;
    }
}

/* pct(x) without its trailing '%' (trim_end_matches). */
static const char *pct_bare(mt_frame *f, float x) {
    char *s = tui_arena_strdup(f->a, mt_pct(f, x));
    size_t n = strlen(s);
    while (n && s[n - 1] == '%') s[--n] = 0;
    return s;
}

size_t mt_load_cells(mt_frame *f, const mt_load_row *r, mt_load_cols lt, tui_table_cell *out) {
    if (!r) {
        const char *h[4];
        size_t n = mt_load_cols_header(lt, h);
        for (size_t i = 0; i < n; i++) out[i] = mt_cell_span(f, mt_fgs("-", MT_FAINT));
        return n;
    }
    tui_line pair = MT_LINE(f, mt_share_span(f, r->share), tui_span_raw(" "), mt_styled(mt_pct(f, r->cpu), mt_busy_style(r->cpu)));
    tui_table_cell meshd = mt_cell_span(f, mt_styled(mt_pct(f, r->meshd), mt_busy_style(r->meshd)));
    switch (lt) {
    case MT_LOAD_COLS_NONE: return 0;
    case MT_LOAD_COLS_ONE: out[0] = mt_cell_line(f, pair); return 1;
    case MT_LOAD_COLS_PAIR:
        out[0] = mt_cell_line(f, pair);
        out[1] = meshd;
        return 2;
    default: break;
    }
    tui_line cpu;
    if (r->little.some)
        cpu = MT_LINE(f, mt_styled(mt_pct(f, r->cpu), mt_busy_style(r->cpu)),
                      mt_fgs(MT_FMT(f, " %s/%s", pct_bare(f, r->big), pct_bare(f, r->little.v)), MT_DIM));
    else
        cpu = tui_line_span(f->a, mt_styled(mt_pct(f, r->cpu), mt_busy_style(r->cpu)));
    tui_line psi;
    if (r->has_psi) {
        float c = r->psi[0], m = r->psi[1];
        psi = MT_LINE(f, mt_styled(mt_ff(f, c * 100.0f, 1), mt_busy_style(c * 10.0f)), mt_fgs("/", MT_DIM),
                      mt_styled(MT_FMT(f, "%s%%", mt_ff(f, m * 100.0f, 1)), mt_busy_style(m * 10.0f)));
    } else if (r->mem_free.some) {
        psi = tui_line_span(f->a, mt_styled(MT_FMT(f, "mem %s", mt_pct(f, r->mem_free.v)), mt_busy_style(1.0f - r->mem_free.v)));
    } else {
        psi = tui_line_span(f->a, mt_fgs("-", MT_FAINT));
    }
    out[0] = mt_cell_span(f, mt_share_span(f, r->share));
    out[1] = mt_cell_line(f, cpu);
    out[2] = mt_cell_line(f, psi);
    out[3] = mt_cell_span(f, mt_styled(mt_pct(f, r->meshd), mt_busy_style(r->meshd)));
    return 4;
}

const char *mt_gb_pair(mt_frame *f, uint32_t a_mb, uint32_t b_mb) {
    return MT_FMT(f, "%s/%sG", mt_ff(f, (float)a_mb / 1024.0f, 0), mt_ff(f, (float)b_mb / 1024.0f, 0));
}

static void cpu_spans(mt_frame *f, const mt_load *l, tui_line *out) {
    tui_line_push(f->a, out, mt_styled(mt_pct(f, l->cpu), mt_busy_style(l->cpu)));
    if (l->cpus_little > 0) {
        tui_line_push(f->a, out, mt_label(" big "));
        tui_line_push(f->a, out, mt_styled(mt_pct(f, l->cpu_big), mt_busy_style(l->cpu_big)));
        tui_line_push(f->a, out, mt_label(" little "));
        tui_line_push(f->a, out, mt_styled(mt_pct(f, l->cpu_little), mt_busy_style(l->cpu_little)));
    }
}

static void pressure_spans(mt_frame *f, const mt_load *l, tui_line *out) {
    if (l->has_psi) {
        const float *p = l->psi;
        tui_line_push(f->a, out, mt_label(" PSI cpu "));
        tui_line_push(f->a, out, mt_styled(MT_FMT(f, "%s%%", mt_ff(f, p[0] * 100.0f, 1)), mt_busy_style(p[0] * 10.0f)));
        tui_line_push(f->a, out, mt_label(" mem "));
        tui_line_push(f->a, out, mt_styled(MT_FMT(f, "%s%%", mt_ff(f, p[1] * 100.0f, 1)), mt_busy_style(p[1] * 10.0f)));
        tui_line_push(f->a, out, mt_label(" io "));
        tui_line_push(f->a, out, mt_styled(MT_FMT(f, "%s%%", mt_ff(f, p[3] * 100.0f, 1)), mt_busy_style(p[3] * 10.0f)));
    } else if (l->has_mem_level) {
        uint32_t lv = l->mem_level;
        const char *t = lv == 1 ? "normal" : lv == 2 ? "warn" : lv == 4 ? "critical" : MT_FMT(f, "?%u", lv);
        tui_line_push(f->a, out, mt_label(" mem pressure "));
        tui_line_push(f->a, out, mt_fgs(t, lv == 1 ? MT_GOOD : lv == 2 ? MT_WARN : MT_BAD));
    }
}

void mt_load_lines(mt_frame *f, const mt_load *l, bool compact, mt_lines *out) {
    if (compact) {
        tui_line s = MT_LINE(f, mt_label("GPU "), mt_share_span(f, l->share), mt_label(" CPU "));
        cpu_spans(f, l, &s);
        pressure_spans(f, l, &s);
        tui_line_push(f->a, &s, mt_label(" meshd "));
        tui_line_push(f->a, &s, mt_styled(mt_pct(f, l->meshd_cpu), mt_busy_style(l->meshd_cpu)));
        mt_lines_push(f, out, s);
        return;
    }
    tui_line g = MT_LINE(f, mt_label("GPU got "), mt_share_span(f, l->share), mt_label(" (10 s "), mt_share_span(f, l->share10),
                         mt_label(")"));
    if (!l->share.some) {
        tui_line_push(f->a, &g, mt_fgs("nothing observed (no idle node, no lease)", MT_DIM));
    } else {
        tui_line_push(f->a, &g, mt_label(" slice "));
        tui_line_push(f->a, &g, mt_val(mt_fmt_us(f, l->slice_us)));
        tui_line_push(f->a, &g, mt_label(" gap "));
        tui_line_push(f->a, &g, mt_val(mt_fmt_us(f, l->gap_us)));
        tui_line_push(f->a, &g, mt_label(" max "));
        tui_line_push(f->a, &g, mt_fgs(mt_fmt_us(f, l->max_gap_us), l->max_gap_us > 10000.0f ? MT_BAD : MT_FG));
        tui_line_push(f->a, &g, mt_label(" ×"));
        tui_line_push(f->a, &g, mt_val(l->inflate.some ? mt_ff(f, l->inflate.v, 2) : "-"));
    }
    if (l->util_ext.some) {
        tui_line_push(f->a, &g, mt_label(" busy "));
        tui_line_push(f->a, &g, mt_fgs(mt_pct(f, l->util_ext.v), MT_DIM));
    }
    tui_line c = tui_line_span(f->a, mt_label("CPU "));
    cpu_spans(f, l, &c);
    tui_line_push(f->a, &c, mt_label(" load "));
    tui_line_push(f->a, &c, mt_val(mt_ff(f, l->loadavg[0], 2)));
    pressure_spans(f, l, &c);
    tui_line m = MT_LINE(f, mt_label("mem "), mt_styled(mt_gb_pair(f, l->mem_avail_mb, l->mem_total_mb), mt_busy_style(1.0f - l->mem_free)),
                         mt_label(" avail"), mt_label("  meshd "), mt_styled(mt_pct(f, l->meshd_cpu), mt_busy_style(l->meshd_cpu)),
                         mt_fgs(MT_FMT(f, " %sM", mt_ff(f, l->meshd_rss_mb, 0)), MT_DIM), mt_label("  disk "),
                         mt_val(MT_FMT(f, "%sG", mt_ff(f, (float)l->disk_free_mb / 1024.0f, 0))), mt_label(" free"));
    if (l->has_thermal) {
        uint32_t t = l->thermal;
        tui_line_push(f->a, &m, mt_label("  thermal "));
        tui_line_push(f->a, &m, mt_fgs(t < MESH_THERMAL_STATES_LEN ? MESH_THERMAL_STATES[t] : "?", t == 0 ? MT_GOOD : t == 1 ? MT_WARN : MT_BAD));
    } else if (l->temp_c.some) {
        float t = l->temp_c.v;
        tui_line_push(f->a, &m, mt_label("  "));
        tui_line_push(f->a, &m, mt_fgs(MT_FMT(f, "%s°C", mt_ff(f, t, 0)), t >= 85.0f ? MT_BAD : MT_DIM));
    }
    mt_lines_push(f, out, g);
    mt_lines_push(f, out, c);
    mt_lines_push(f, out, m);
}

void mt_draw_load_box(mt_frame *f, tui_rect area, const mt_snapshot *s, const mt_lines *lines) {
    if (area.height == 0 || area.width == 0) return;
    mt_load l;
    const char *age = "no sample ";
    if (mt_snapshot_load(s, &l)) age = l.age_ms > 3000 ? MT_FMT(f, "stale %ss ", mt_ff(f, (float)l.age_ms / 1000.0f, 0)) : "";
    tui_line title = MT_LINE(f, mt_fgb(" Load ", MT_FG), mt_fgs(age, MT_WARN));
    tui_block b = tui_block_bordered();
    b.set = &TUI_BORDER_ROUNDED;
    b.border_style = tui_fg(MT_FAINT);
    tui_block_add_title(f->a, &b, title, TUI_TITLE_DEFAULT);
    tui_rect inner = tui_block_inner(&b, area);
    mt_block(f, &b, area);
    mt_par(f, mt_text(f, lines), inner);
}

/* One cell per logical CPU: a block whose height is its busy share, big cores bright, little ones dim. */
static tui_span core_bar(uint8_t u, uint8_t c) {
    static const char *const BARS[9] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    if (u == 0xff) return mt_fgs("·", MT_FAINT);
    size_t i = ((size_t)u * 8 + 50) / 100;
    static const uint8_t little[3] = {110, 150, 200}, big[3] = {110, 190, 255};
    tui_color col = u >= 90 ? MT_BAD : u >= 60 ? MT_WARN : mt_rgb(c == MESH_CORE_LITTLE ? little : big, c == MESH_CORE_LITTLE ? 0.7f : 1.0f);
    return mt_styled(BARS[i < 8 ? i : 8], tui_fg_bg(col, MT_FAINT));
}

typedef struct {
    const char *name;
    uint16_t w;
    uint8_t tier;
} col;

void mt_draw_load(mt_frame *f, tui_rect area, const mt_app *app) {
    uint16_t n = (uint16_t)app->nmachines;
    const mt_machine *m = mt_app_focused(app);
    const char *pnames[MESH_PROG_SLOTS];
    mt_prog_load ploads[MESH_PROG_SLOTS];
    size_t nprogs = 0;
    if (m->has_snap)
        for (size_t i = 0; i < m->snap.nsections; i++) {
            const mt_section *p = &m->snap.sections[i];
            if (mt_snapshot_prog_load(&m->snap, mt_section_slot(p), &ploads[nprogs])) pnames[nprogs++] = mt_sec_name(f, p);
        }
    tui_rect r[4];
    tui_constraint cons[4] = {TUI_LENGTH(tui_min16((uint16_t)(n + 3), area.height)), TUI_LENGTH(4), TUI_LENGTH(nprogs + 3), TUI_MAX(12)};
    tui_vsplit(area, 4, cons, r);
    tui_rect mach = r[0], cores = r[1], prog = r[2], hist = r[3];
    /* Columns by tier: 0 always, 1 from 130 columns, 2 from 150. */
    int tier = area.width >= 150 ? 2 : area.width >= 130 ? 1 : 0;
    static const col cols[22] = {
        {"machine", 14, 0}, {"GPU got", 7, 0}, {"10 s", 5, 0}, {"slice", 7, 1}, {"gap", 7, 1}, {"max gap", 7, 0},
        {"gaps/s", 6, 2}, {"seen", 5, 2}, {"×lat", 5, 1}, {"queue", 6, 2}, {"busy ext", 8, 2}, {"CPU", 5, 0},
        {"big", 5, 0}, {"little", 6, 0}, {"load", 5, 1}, {"PSI cpu/mem/io", 15, 0}, {"mem avail", 9, 0}, {"swap", 8, 2},
        {"meshd", 5, 0}, {"rss", 5, 1}, {"disk free", 9, 1}, {"temp", 0, 1},
    };
    bool keep[22];
    const char *header[22];
    tui_constraint widths[22];
    size_t nh = 0;
    for (size_t i = 0; i < 22; i++) {
        keep[i] = cols[i].tier <= tier;
        if (keep[i]) {
            widths[nh] = cols[i].w == 0 ? TUI_FILL(1) : TUI_LENGTH(cols[i].w);
            header[nh++] = cols[i].name;
        }
    }
    mt_rows rows = {0};
    for (size_t i = 0; i < app->nmachines; i++) {
        const mt_machine *mi = &app->machines[i];
        tui_span name = mt_fgb(mt_name(f, mi), MT_MACHINE[i % 8]);
        mt_load l;
        if (!mi->has_snap || !mt_snapshot_load(&mi->snap, &l)) {
            tui_table_cell c2[2] = {mt_cell_span(f, name), mt_cell_span(f, mt_fgs(mi->has_snap ? "no load sample" : mi->status, MT_DIM))};
            mt_rows_push(f, &rows, mt_row(f, c2, 2));
            continue;
        }
        bool g = l.share.some;
        const char *psi;
        if (l.has_psi)
            psi = MT_FMT(f, "%s/%s/%s%%", mt_ff(f, l.psi[0] * 100.0f, 1), mt_ff(f, l.psi[1] * 100.0f, 1), mt_ff(f, l.psi[3] * 100.0f, 1));
        else if (l.has_mem_level && l.mem_level == 1)
            psi = "mem normal";
        else if (l.has_mem_level && l.mem_level == 2)
            psi = "mem warn";
        else if (l.has_mem_level && l.mem_level == 4)
            psi = "mem critical";
        else
            psi = "-";
        const char *temp;
        if (l.has_thermal)
            temp = l.thermal < MESH_THERMAL_STATES_LEN ? MESH_THERMAL_STATES[l.thermal] : "?";
        else if (l.temp_c.some)
            temp = MT_FMT(f, "%s°C", mt_ff(f, l.temp_c.v, 0));
        else
            temp = "-";
        tui_table_cell all[22] = {
            mt_cell_span(f, name),
            mt_cell_span(f, mt_share_span(f, l.share)),
            mt_cell_span(f, mt_share_span(f, l.share10)),
            mt_cell_str(f, g ? mt_fmt_us(f, l.slice_us) : "-"),
            mt_cell_str(f, g ? mt_fmt_us(f, l.gap_us) : "-"),
            mt_cell_span(f, mt_fgs(mt_fmt_us(f, l.max_gap_us), l.max_gap_us > 10000.0f ? MT_BAD : MT_FG)),
            mt_cell_str(f, mt_ff(f, l.gaps_s, 0)),
            mt_cell_span(f, mt_fgs(mt_ff(f, l.observed, 1), MT_DIM)),
            mt_cell_str(f, l.inflate.some ? mt_ff(f, l.inflate.v, 2) : "-"),
            mt_cell_str(f, l.queue_us > 0.0f ? mt_fmt_us(f, l.queue_us) : "-"),
            mt_cell_str(f, l.util_ext.some ? mt_pct(f, l.util_ext.v) : "-"),
            mt_cell_span(f, mt_styled(mt_pct(f, l.cpu), mt_busy_style(l.cpu))),
            mt_cell_span(f, mt_styled(mt_pct(f, l.cpu_big), mt_busy_style(l.cpu_big))),
            mt_cell_span(f, l.cpus_little > 0 ? mt_styled(mt_pct(f, l.cpu_little), mt_busy_style(l.cpu_little)) : mt_fgs("-", MT_FAINT)),
            mt_cell_str(f, mt_ff(f, l.loadavg[0], 2)),
            mt_cell_str(f, psi),
            mt_cell_span(f, mt_styled(mt_gb_pair(f, l.mem_avail_mb, l.mem_total_mb), mt_busy_style(1.0f - l.mem_free))),
            mt_cell_str(f, mt_gb_pair(f, l.swap_used_mb, l.swap_total_mb)),
            mt_cell_span(f, mt_styled(mt_pct(f, l.meshd_cpu), mt_busy_style(l.meshd_cpu))),
            mt_cell_str(f, MT_FMT(f, "%sM", mt_ff(f, l.meshd_rss_mb, 0))),
            mt_cell_str(f, MT_FMT(f, "%sG", mt_ff(f, (float)l.disk_free_mb / 1024.0f, 0))),
            mt_cell_str(f, temp),
        };
        tui_table_cell kept[22];
        size_t nk = 0;
        for (size_t c = 0; c < 22; c++)
            if (keep[c]) kept[nk++] = all[c];
        mt_rows_push(f, &rows, mt_row(f, kept, nk));
    }
    tui_line title = MT_LINE(f, mt_fgb(" load ", MT_FG),
                             mt_label("GPU got: the share of wall time our work ran (measured inside our kernels / per Metal command "
                                      "buffer); ×lat = 1 / share "));
    tui_block pb = mt_panel(f, title, MT_FAINT);
    tui_table_row hdr = mt_header(f, header, nh);
    tui_table t = tui_table_new(rows.rows, rows.n, widths, nh);
    t.header = &hdr;
    t.column_spacing = 1;
    t.block = &pb;
    mt_table(f, &t, mach);

    /* The focused machine's cores. */
    if (cores.height > 0) {
        tui_line ctitle;
        mt_lines lines = {0};
        mt_load l;
        if (m->has_snap && mt_snapshot_load(&m->snap, &l)) {
            tui_line big = tui_line_span(f->a, mt_label(MT_FMT(f, "%s%s  ", mt_pad(f, "big", 7, '<'), mt_pad(f, MT_FMT(f, "%u", l.cpus_big), 3, '>'))));
            tui_line little = tui_line_span(f->a, mt_label(MT_FMT(f, "%s%s  ", mt_pad(f, "little", 7, '<'), mt_pad(f, MT_FMT(f, "%u", l.cpus_little), 3, '>'))));
            tui_line other = tui_line_span(f->a, mt_label(MT_FMT(f, "%s%s  ", mt_pad(f, "cpus", 7, '<'), mt_pad(f, MT_FMT(f, "%u", l.cpus), 3, '>'))));
            for (size_t i = 0; i < l.ncores; i++) {
                tui_span bar = core_bar(l.cores[i][0], l.cores[i][1]);
                if (l.cores[i][1] == MESH_CORE_BIG) tui_line_push(f->a, &big, bar);
                else if (l.cores[i][1] == MESH_CORE_LITTLE) tui_line_push(f->a, &little, bar);
                else tui_line_push(f->a, &other, bar);
            }
            if (l.cpus_little > 0) {
                mt_lines_push(f, &lines, big);
                mt_lines_push(f, &lines, little);
            } else if (l.cpus_big > 0) {
                mt_lines_push(f, &lines, big);
            } else {
                mt_lines_push(f, &lines, other);
            }
            ctitle = MT_LINE(f, mt_fgb(MT_FMT(f, " %s cores ", mt_name(f, m)), MT_FG), mt_label("busy per logical CPU, last second "));
        } else {
            ctitle = tui_line_span(f->a, mt_fgb(" cores ", MT_FG));
            mt_lines_push(f, &lines, tui_line_span(f->a, mt_label("no load sample")));
        }
        tui_block cb = mt_panel(f, ctitle, MT_FAINT);
        tui_paragraph p = tui_paragraph_new(mt_text(f, &lines));
        p.block = &cb;
        tui_paragraph_render(f->a, &p, cores, f->buf);
    }

    /* The focused machine's programs. */
    if (prog.height > 0) {
        static const char *const ph[15] = {"program", "GPU got", "10 s", "slice", "max gap", "seen", "p50", "p90",
                                           "p99", "max", "n", "per task", "tasks/s", "busy", "queue"};
        mt_rows prows = {0};
        for (size_t i = 0; i < nprogs; i++) {
            const mt_prog_load *l = &ploads[i];
#define Q(x) ((x).some ? mt_fmt_us(f, (x).v) : "-")
            tui_table_cell c[15] = {
                mt_cell_span(f, mt_fgb(pnames[i], MT_FG)),
                mt_cell_span(f, mt_share_span(f, l->share)),
                mt_cell_span(f, mt_share_span(f, l->share10)),
                mt_cell_str(f, l->share.some ? mt_fmt_us(f, l->slice_us) : "-"),
                mt_cell_str(f, mt_fmt_us(f, l->max_gap_us)),
                mt_cell_span(f, mt_fgs(mt_ff(f, l->observed, 1), MT_DIM)),
                mt_cell_str(f, Q(l->p50_us)),
                mt_cell_str(f, Q(l->p90_us)),
                mt_cell_str(f, Q(l->p99_us)),
                mt_cell_str(f, Q(l->max_us)),
                mt_cell_span(f, mt_fgs(MT_FMT(f, "%u", l->lat_n), MT_DIM)),
                mt_cell_str(f, Q(l->task_us)),
                mt_cell_span(f, mt_fgs(mt_fmt_rate(f, l->tasks_s), MT_WARN)),
                mt_cell_span(f, mt_styled(mt_pct(f, l->busy), mt_busy_style(l->busy))),
                mt_cell_str(f, l->queue_us > 0.0f ? mt_fmt_us(f, l->queue_us) : "-"),
            };
#undef Q
            mt_rows_push(f, &prows, mt_row(f, c, 15));
        }
        static const tui_constraint pw[15] = {
            {TUI_C_LENGTH, 15, 0}, {TUI_C_LENGTH, 7, 0}, {TUI_C_LENGTH, 5, 0}, {TUI_C_LENGTH, 7, 0}, {TUI_C_LENGTH, 7, 0},
            {TUI_C_LENGTH, 5, 0},  {TUI_C_LENGTH, 7, 0}, {TUI_C_LENGTH, 7, 0}, {TUI_C_LENGTH, 7, 0}, {TUI_C_LENGTH, 7, 0},
            {TUI_C_LENGTH, 5, 0},  {TUI_C_LENGTH, 8, 0}, {TUI_C_LENGTH, 8, 0}, {TUI_C_LENGTH, 5, 0}, {TUI_C_FILL, 1, 0},
        };
        tui_line ptitle = MT_LINE(f, mt_fgb(MT_FMT(f, " %s programs ", mt_name(f, m)), MT_FG),
                                  mt_label("task time from the trace (10 s), per task = node busy / tasks, seen = node-s observed idle per s "));
        tui_block pb2 = mt_panel(f, ptitle, MT_FAINT);
        tui_table_row phdr = mt_header(f, ph, 15);
        tui_table pt = tui_table_new(prows.rows, prows.n, pw, 15);
        pt.header = &phdr;
        pt.column_spacing = 1;
        pt.block = &pb2;
        mt_table(f, &pt, prog);
    }

    /* GPU share and CPU history, one column per sample. */
    if (hist.height > 2) {
        size_t w = tui_sat_sub(hist.width, 2);
        size_t nh2 = m->nload_hist;
        uint64_t *share = tui_arena_alloc(f->a, (nh2 ? nh2 : 1) * sizeof *share);
        for (size_t i = 0; i < nh2; i++) share[i] = m->load_hist[i][0];
        size_t from = nh2 > w ? nh2 - w : 0;
        uint64_t cpu = nh2 ? m->load_hist[nh2 - 1][1] : 0;
        tui_line htitle = MT_LINE(f, mt_fgb(MT_FMT(f, " %s GPU got, %% per second ", mt_name(f, m)), MT_FG),
                                  mt_label(MT_FMT(f, "CPU now %llu%% ", (unsigned long long)cpu)));
        tui_block hb = mt_panel(f, htitle, MT_FAINT);
        tui_sparkline sp = {0};
        sp.data = share + from;
        sp.n = nh2 - from;
        sp.has_max = true;
        sp.max = 100;
        sp.style = tui_fg(MT_ACCENT);
        sp.block = &hb;
        tui_sparkline_render(f->a, &sp, hist, f->buf);
    }
}
