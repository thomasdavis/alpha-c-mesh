/* ui.rs's palette and helpers, ported. Float math stays in f32 where the Rust's does. */
#include "mesh/top_ui_kit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

const tui_color MT_MACHINE[8] = {
    {TUI_COLOR_RGB, 90, 210, 255}, {TUI_COLOR_RGB, 255, 150, 80}, {TUI_COLOR_RGB, 150, 240, 120}, {TUI_COLOR_RGB, 240, 120, 200},
    {TUI_COLOR_RGB, 250, 230, 90}, {TUI_COLOR_RGB, 170, 140, 255}, {TUI_COLOR_RGB, 80, 230, 200}, {TUI_COLOR_RGB, 230, 230, 230},
};

const uint8_t MT_WALL_RGB[7][3] = {
    {210, 60, 50}, {70, 185, 85}, {70, 115, 225}, {225, 190, 60}, {175, 85, 210}, {60, 195, 205}, {205, 205, 210},
};

static const struct {
    const char *name;
    uint8_t rgb[3];
} NAMED_RGB[4] = {{"doom", {255, 110, 60}}, {"train", {80, 220, 110}}, {"infer", {80, 160, 255}}, {"http", {200, 110, 255}}};
static const uint8_t SLOT_RGB[6][3] = {{240, 200, 70}, {60, 200, 200}, {240, 120, 180}, {170, 200, 90}, {150, 140, 255}, {230, 150, 90}};

tui_color mt_sys_color(uint32_t k) { return MT_MACHINE[k % 8]; }

void mt_prog_rgb(const char *name, uint32_t slot, uint8_t out[3]) {
    for (size_t i = 0; i < 4; i++)
        if (strcmp(NAMED_RGB[i].name, name) == 0) {
            memcpy(out, NAMED_RGB[i].rgb, 3);
            return;
        }
    memcpy(out, SLOT_RGB[slot % 6], 3);
}

uint8_t mt_sat_u8(float v) {
    if (!(v > 0.0f)) return 0;
    if (v >= 255.0f) return 255;
    return (uint8_t)v;
}

uint16_t mt_sat_u16(double v) {
    if (!(v > 0.0)) return 0;
    if (v >= 65535.0) return 65535;
    return (uint16_t)v;
}

uint32_t mt_sat_u32(double v) {
    if (!(v > 0.0)) return 0;
    if (v >= 4294967295.0) return UINT32_MAX;
    return (uint32_t)v;
}

uint64_t mt_sat_u64(double v) {
    if (!(v > 0.0)) return 0;
    if (v >= 18446744073709551615.0) return UINT64_MAX;
    return (uint64_t)v;
}

int32_t mt_sat_i32(double v) {
    if (v != v) return 0;
    if (v <= -2147483648.0) return INT32_MIN;
    if (v >= 2147483647.0) return INT32_MAX;
    return (int32_t)v;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

tui_color mt_rgb(const uint8_t c[3], float k) {
    uint8_t o[3];
    for (int i = 0; i < 3; i++) o[i] = mt_sat_u8(clampf((float)c[i] * k, 0.0f, 255.0f));
    return TUI_RGB(o[0], o[1], o[2]);
}

tui_color mt_lerp(const uint8_t a[3], const uint8_t b[3], float t) {
    t = clampf(t, 0.0f, 1.0f);
    uint8_t o[3];
    for (int i = 0; i < 3; i++) o[i] = mt_sat_u8((float)a[i] + ((float)b[i] - (float)a[i]) * t);
    return TUI_RGB(o[0], o[1], o[2]);
}

tui_color mt_prog_color(const char *name, uint32_t slot, float k) {
    uint8_t c[3];
    mt_prog_rgb(name, slot, c);
    return mt_rgb(c, k);
}

/* ---- text */

const char *mt_ff(mt_frame *f, double v, int prec) {
    char b[400];
    rsfmt_fixed(b, sizeof b, v, prec, 0);
    return tui_arena_strdup(f->a, b);
}

const char *mt_ffp(mt_frame *f, double v, int prec) {
    char b[400];
    rsfmt_fixed(b, sizeof b, v, prec, RSFMT_PLUS);
    return tui_arena_strdup(f->a, b);
}

const char *mt_fe(mt_frame *f, double v, int prec) {
    char b[64];
    rsfmt_exp(b, sizeof b, v, prec, 0);
    return tui_arena_strdup(f->a, b);
}

const char *mt_pad(mt_frame *f, const char *s, int w, char align) {
    size_t n = strlen(s) + (w > 0 ? (size_t)w : 0) + 1;
    char *o = tui_arena_alloc(f->a, n);
    rsfmt_pad(o, n, s, w, align, ' ');
    return o;
}

const char *mt_take(mt_frame *f, const char *s, size_t n) { return tui_arena_strndup(f->a, s, rsfmt_prefix(s, n)); }

const char *mt_name(mt_frame *f, const mt_machine *m) {
    size_t cap = 98 + strlen(m->endpoint);
    char *o = tui_arena_alloc(f->a, cap);
    mt_machine_name(m, o, cap);
    return o;
}

tui_table_cell mt_cell_str(mt_frame *f, const char *s) { return tui_table_cell_text(tui_text_from_str(f->a, s)); }

tui_table_cell mt_cell_span(mt_frame *f, tui_span s) { return tui_table_cell_text(tui_text_line(f->a, tui_line_span(f->a, s))); }

tui_table_cell mt_cell_line(mt_frame *f, tui_line l) { return tui_table_cell_text(tui_text_line(f->a, l)); }

tui_table_cell mt_cell_styled(mt_frame *f, const char *s, tui_style st) {
    tui_table_cell c = mt_cell_str(f, s);
    c.style = st;
    return c;
}

tui_table_row mt_row(mt_frame *f, const tui_table_cell *cells, size_t n) {
    tui_table_row r = tui_table_row_new();
    for (size_t i = 0; i < n; i++) tui_table_row_push(f->a, &r, cells[i]);
    return r;
}

tui_table_row mt_header(mt_frame *f, const char *const *names, size_t n) {
    tui_table_row r = tui_table_row_new();
    for (size_t i = 0; i < n; i++) tui_table_row_push(f->a, &r, mt_cell_span(f, mt_label(names[i])));
    return r;
}

void mt_rows_push(mt_frame *f, mt_rows *r, tui_table_row row) {
    if (r->n == r->cap) {
        size_t cap = r->cap ? r->cap * 2 : 8;
        r->rows = tui_arena_grow(f->a, r->rows, r->cap * sizeof *r->rows, cap * sizeof *r->rows);
        r->cap = cap;
    }
    r->rows[r->n++] = row;
}

void mt_cells_push(mt_frame *f, mt_cells *c, tui_table_cell cell) {
    if (c->n == c->cap) {
        size_t cap = c->cap ? c->cap * 2 : 16;
        c->c = tui_arena_grow(f->a, c->c, c->cap * sizeof *c->c, cap * sizeof *c->c);
        c->cap = cap;
    }
    c->c[c->n++] = cell;
}

void mt_lines_push(mt_frame *f, mt_lines *l, tui_line line) {
    if (l->n == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 8;
        l->l = tui_arena_grow(f->a, l->l, l->cap * sizeof *l->l, cap * sizeof *l->l);
        l->cap = cap;
    }
    l->l[l->n++] = line;
}

tui_text mt_text(mt_frame *f, const mt_lines *l) { return tui_text_of(f->a, l->n, l->l); }

tui_block mt_panel(mt_frame *f, tui_line title, tui_color color) {
    tui_block b = tui_block_bordered();
    b.set = &TUI_BORDER_ROUNDED;
    b.border_style = tui_fg(color);
    tui_block_add_title(f->a, &b, title, TUI_TITLE_DEFAULT);
    b.style = tui_fg_bg(MT_FG, MT_BG);
    return b;
}

void mt_par(mt_frame *f, tui_text t, tui_rect area) {
    tui_paragraph p = tui_paragraph_new(t);
    tui_paragraph_render(f->a, &p, area, f->buf);
}

void mt_par_line(mt_frame *f, tui_line l, tui_rect area) { mt_par(f, tui_text_line(f->a, l), area); }

void mt_block(mt_frame *f, const tui_block *b, tui_rect area) { tui_block_render(f->a, b, area, f->buf); }

void mt_table(mt_frame *f, const tui_table *t, tui_rect area) {
    tui_table_state st = {0};
    tui_table_render(f->a, t, area, f->buf, &st);
}

/* ---- formats */

const char *mt_fmt_uptime(mt_frame *f, uint64_t ms) {
    uint64_t s = ms / 1000;
    if (s >= 3600)
        return MT_FMT(f, "%lluh%02llum%02llus", (unsigned long long)(s / 3600), (unsigned long long)(s / 60 % 60),
                      (unsigned long long)(s % 60));
    return MT_FMT(f, "%llum%02llus", (unsigned long long)(s / 60), (unsigned long long)(s % 60));
}

const char *mt_fmt_rate(mt_frame *f, float r) {
    if (r >= 1e6f) return MT_FMT(f, "%sM", mt_ff(f, r / 1e6f, 2));
    if (r >= 1e4f) return MT_FMT(f, "%sk", mt_ff(f, r / 1e3f, 1));
    return mt_ff(f, r, 1);
}

const char *mt_fmt_us(mt_frame *f, float us) {
    if (!isfinite(us) || us <= 0.0f) return "0";
    if (us >= 1000.0f) return MT_FMT(f, "%sms", mt_ff(f, us / 1000.0f, 1));
    return MT_FMT(f, "%sus", mt_ff(f, us, 0));
}

const char *mt_fmt_bytes(mt_frame *f, uint64_t b) {
    if (b == 0) return "-";
    if (b >= (uint64_t)1 << 30) return MT_FMT(f, "%sG", mt_ff(f, (double)b / (double)((uint64_t)1 << 30), 1));
    return MT_FMT(f, "%sM", mt_ff(f, (double)b / (double)((uint64_t)1 << 20), 1));
}

const char *mt_pct(mt_frame *f, float x) { return MT_FMT(f, "%s%%", mt_ff(f, clampf(x * 100.0f, 0.0f, 999.0f), 0)); }

const char *mt_mask_list(mt_frame *f, uint32_t mask) {
    char b[128];
    size_t n = 0;
    b[0] = 0;
    for (uint32_t k = 0; k < 32; k++)
        if (mask & (1u << k)) n += (size_t)snprintf(b + n, sizeof b - n, "%s%u", n ? "," : "", k);
    return tui_arena_strdup(f->a, b);
}

const char *mt_short_gpu(mt_frame *f, const char *g) {
    while (strncmp(g, "NVIDIA ", 7) == 0) g += 7;
    while (strncmp(g, "GeForce ", 8) == 0) g += 8;
    return tui_arena_strdup(f->a, g);
}

/* ---- styles */

tui_style mt_state_style(const mt_section *s) {
    switch (mt_section_state(s)) {
    case MESH_ST_RUNNING: return tui_fg_bold(MT_GOOD);
    case MESH_ST_FAILED: return tui_fg_bold(MT_BAD);
    case MESH_ST_STOPPING: return tui_fg(MT_WARN);
    default: return tui_fg(MT_DIM);
    }
}

tui_style mt_share_style(float x) { return tui_fg_bold(x >= 0.8f ? MT_GOOD : x >= 0.35f ? MT_WARN : MT_BAD); }

tui_style mt_busy_style(float x) {
    return tui_fg(x >= 0.9f ? MT_BAD : x >= 0.6f ? MT_WARN : x < 0.05f ? MT_DIM : MT_FG);
}

tui_span mt_share_span(mt_frame *f, mt_opt_f32 s) {
    if (s.some) return mt_styled(mt_pct(f, s.v), mt_share_style(s.v));
    return mt_fgs("-", MT_FAINT);
}

tui_span mt_peer_span(mt_frame *f, const mt_snapshot *s) {
    uint32_t x = mt_snapshot_host_u32(s, MESH_S_PEER_STATE);
    switch (x) {
    case 0: return mt_fgs("single", MT_DIM);
    case 1: return mt_fgb("waiting for members", MT_WARN);
    case 2: return mt_fgb("peered", MT_GOOD);
    default: return mt_fgs(MT_FMT(f, "state %u", x), MT_BAD);
    }
}

tui_line mt_machine_title(mt_frame *f, const mt_machine *m, bool focused, const char *extra, tui_color *color) {
    tui_line l = tui_line_empty();
    if (focused) tui_line_push(f->a, &l, mt_fgs(" ◆", MT_ACCENT));
    tui_line_push(f->a, &l, mt_fgb(MT_FMT(f, " %s ", mt_name(f, m)), MT_FG));
    if (!m->online) {
        tui_line_push(f->a, &l, mt_styled(" OFFLINE ", mt_fg_bg_bold(TUI_WHITE, MT_BAD)));
        *color = MT_BAD;
    } else {
        *color = focused ? MT_ACCENT : MT_FAINT;
    }
    if (*extra) tui_line_push(f->a, &l, mt_fgs(MT_FMT(f, "%s ", extra), MT_DIM));
    return l;
}

void mt_no_data(mt_frame *f, tui_rect area, const mt_machine *m) {
    tui_line ls[3] = {
        tui_line_span(f->a, mt_fgs("no snapshot yet", MT_DIM)),
        MT_LINE(f, mt_label("endpoint "), mt_val(m->endpoint)),
        MT_LINE(f, mt_label("status   "), mt_fgs(m->status, MT_WARN)),
    };
    mt_par(f, tui_text_of(f->a, 3, ls), area);
}

void mt_not_loaded(mt_frame *f, tui_rect area, const mt_machine *m, const char *name) {
    tui_line ls[2] = {
        tui_line_span(f->a, mt_styled(MT_FMT(f, " %s is not loaded on %s ", name, mt_name(f, m)), mt_fg_bg_bold(MT_BG, MT_WARN))),
        tui_line_span(f->a, mt_label(MT_FMT(f, "Programs tab (2), then L to load %s from the catalog", name))),
    };
    tui_paragraph p = tui_paragraph_new(tui_text_of(f->a, 2, ls));
    p.alignment = TUI_ALIGN_CENTER;
    uint16_t y = (uint16_t)(area.y + area.height / 2);
    uint16_t top = tui_max16(tui_sat_sub(y, 1), area.y);
    tui_paragraph_render(f->a, &p, mt_rect(area.x, top, area.width, tui_min16(area.height, 2)), f->buf);
}

const mt_section *mt_bound(mt_frame *f, tui_rect inner, const mt_machine *m, const char *name) {
    if (!m->has_snap) {
        mt_no_data(f, inner, m);
        return NULL;
    }
    const mt_section *p = mt_snapshot_by_name(&m->snap, name);
    if (!p) mt_not_loaded(f, inner, m, name);
    return p;
}

/* f32::total_cmp on finite values: -0 sorts before +0. */
static int total_cmp(float a, float b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return (int)(signbit(b) != 0) - (int)(signbit(a) != 0);
}

bool mt_slowest_wait(const mt_snapshot *s, uint32_t *k, float *us) {
    uint32_t me;
    if (!mt_snapshot_system(s, &me)) return false;
    mt_member ms[MESH_MAX_SYSTEMS];
    size_t n = mt_snapshot_members(s, ms);
    bool any = false;
    for (size_t i = 0; i < n; i++) {
        if (ms[i].system == me || !isfinite(ms[i].wait_us)) continue;
        /* max_by: the last of equal maxima wins */
        if (!any || total_cmp(ms[i].wait_us, *us) >= 0) {
            *k = ms[i].system;
            *us = ms[i].wait_us;
            any = true;
        }
    }
    return any;
}

const char *mt_sec_name(mt_frame *f, const mt_section *p) {
    char b[3 * MESH_NAME_BYTES + 1];
    mt_section_name(p, b);
    return tui_arena_strdup(f->a, b);
}
