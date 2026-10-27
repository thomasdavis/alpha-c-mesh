/* @ajax/mesh-top-ui-kit: what every mesh-top pane shares (crates/mesh-top src/ui.rs's palette and
 * helpers): the dark palette, label/value spans, the rounded panel, machine titles, the number
 * formats (Rust's, through rsfmt) and the "no snapshot" / "not loaded" fallbacks. A frame is an
 * arena (every string, span and line of one draw) and the buffer it draws into. */
#ifndef MESH_TOP_UI_KIT_H
#define MESH_TOP_UI_KIT_H

#include "mesh/top_app.h"
#include "rsfmt.h"
#include "tui/block.h"
#include "tui/layout.h"
#include "tui/paragraph.h"
#include "tui/table.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    tui_arena *a;
    tui_buffer *buf;
} mt_frame;

/* ---- palette */
#define MT_BG TUI_RGB(12, 14, 20)
#define MT_FG TUI_RGB(205, 210, 220)
#define MT_DIM TUI_RGB(105, 110, 128)
#define MT_FAINT TUI_RGB(45, 48, 60)
#define MT_ACCENT TUI_RGB(110, 190, 255)
#define MT_GOOD TUI_RGB(90, 220, 120)
#define MT_WARN TUI_RGB(240, 200, 70)
#define MT_BAD TUI_RGB(240, 70, 70)
/* One colour per system (charts, doom's part markers). */
extern const tui_color MT_MACHINE[8];
tui_color mt_sys_color(uint32_t k);
/* Doom wall kinds 1..7. */
extern const uint8_t MT_WALL_RGB[7][3];
/* The four original programs keep their colours; others take one by slot. */
void mt_prog_rgb(const char *name, uint32_t slot, uint8_t out[3]);
tui_color mt_rgb(const uint8_t c[3], float k);
tui_color mt_lerp(const uint8_t a[3], const uint8_t b[3], float t);
tui_color mt_prog_color(const char *name, uint32_t slot, float k);

/* ---- Rust's saturating float casts (`as u8` etc.: NaN is 0) */
uint8_t mt_sat_u8(float v);
uint16_t mt_sat_u16(double v);
uint32_t mt_sat_u32(double v);
uint64_t mt_sat_u64(double v);
int32_t mt_sat_i32(double v);

/* ---- text in the frame's arena */
#define MT_FMT(f, ...) tui_arena_fmt((f)->a, __VA_ARGS__)
/* A line of spans: MT_LINE(f, span, span, ...). */
#define MT_LINE(f, ...) \
    tui_line_of((f)->a, sizeof((tui_span[]){__VA_ARGS__}) / sizeof(tui_span), (tui_span[]){__VA_ARGS__})
/* {:.prec} of v */
const char *mt_ff(mt_frame *f, double v, int prec);
/* {:+.prec} */
const char *mt_ffp(mt_frame *f, double v, int prec);
/* {:.prec e} */
const char *mt_fe(mt_frame *f, double v, int prec);
/* {:<w} '<', {:>w} '>', {:^w} '^' of a string, by characters */
const char *mt_pad(mt_frame *f, const char *s, int w, char align);
/* The first n characters (Rust's s.chars().take(n)). */
const char *mt_take(mt_frame *f, const char *s, size_t n);
const char *mt_name(mt_frame *f, const mt_machine *m);

static inline tui_span mt_label(const char *s) { return tui_span_styled(s, tui_fg(MT_DIM)); }
static inline tui_span mt_val(const char *s) { return tui_span_styled(s, tui_fg_bold(MT_FG)); }
static inline tui_span mt_styled(const char *s, tui_style st) { return tui_span_styled(s, st); }
static inline tui_span mt_fgs(const char *s, tui_color c) { return tui_span_styled(s, tui_fg(c)); }
static inline tui_span mt_fgb(const char *s, tui_color c) { return tui_span_styled(s, tui_fg_bold(c)); }
static inline tui_style mt_fg_bg_bold(tui_color fg, tui_color bg) { return tui_style_bold(tui_fg_bg(fg, bg)); }

/* A table cell: from a string (Text::from), a span, a line; with a cell style. */
tui_table_cell mt_cell_str(mt_frame *f, const char *s);
tui_table_cell mt_cell_span(mt_frame *f, tui_span s);
tui_table_cell mt_cell_line(mt_frame *f, tui_line l);
tui_table_cell mt_cell_styled(mt_frame *f, const char *s, tui_style st); /* Cell::from(s).style(st) */
/* A row from cells; a header row of DIM labels. */
tui_table_row mt_row(mt_frame *f, const tui_table_cell *cells, size_t n);
tui_table_row mt_header(mt_frame *f, const char *const *names, size_t n);
/* A growable row list in the arena. */
typedef struct {
    tui_table_row *rows;
    size_t n, cap;
} mt_rows;
void mt_rows_push(mt_frame *f, mt_rows *r, tui_table_row row);
/* A growable cell list in the arena. */
typedef struct {
    tui_table_cell *c;
    size_t n, cap;
} mt_cells;
void mt_cells_push(mt_frame *f, mt_cells *c, tui_table_cell cell);
/* A growable line list. */
typedef struct {
    tui_line *l;
    size_t n, cap;
} mt_lines;
void mt_lines_push(mt_frame *f, mt_lines *l, tui_line line);
tui_text mt_text(mt_frame *f, const mt_lines *l);
/* Spans gathered into a line. */
typedef struct {
    tui_line l;
} mt_spans;

/* The rounded panel every pane uses. */
tui_block mt_panel(mt_frame *f, tui_line title, tui_color color);
/* Renders a widget-less paragraph of lines / one line / one span into area. */
void mt_par(mt_frame *f, tui_text t, tui_rect area);
void mt_par_line(mt_frame *f, tui_line l, tui_rect area);
void mt_block(mt_frame *f, const tui_block *b, tui_rect area);
void mt_table(mt_frame *f, const tui_table *t, tui_rect area);

/* ---- formats */
const char *mt_fmt_uptime(mt_frame *f, uint64_t ms);
const char *mt_fmt_rate(mt_frame *f, float r);
const char *mt_fmt_us(mt_frame *f, float us);
const char *mt_fmt_bytes(mt_frame *f, uint64_t b);
const char *mt_pct(mt_frame *f, float x);
/* Systems named in a link mask: "1,2". */
const char *mt_mask_list(mt_frame *f, uint32_t mask);
const char *mt_short_gpu(mt_frame *f, const char *g);

/* ---- styles */
tui_style mt_state_style(const mt_section *s);
tui_style mt_share_style(float x);
tui_style mt_busy_style(float x);
tui_span mt_share_span(mt_frame *f, mt_opt_f32 s);
tui_span mt_peer_span(mt_frame *f, const mt_snapshot *s);

/* A machine's panel title, with focus and OFFLINE marks; *color is the border's. */
tui_line mt_machine_title(mt_frame *f, const mt_machine *m, bool focused, const char *extra, tui_color *color);
void mt_no_data(mt_frame *f, tui_rect area, const mt_machine *m);
/* A custom tab whose program is absent from the machine's table. */
void mt_not_loaded(mt_frame *f, tui_rect area, const mt_machine *m, const char *name);
/* The shared head of a custom tab: its program on the machine, or NULL after drawing why not. */
const mt_section *mt_bound(mt_frame *f, tui_rect inner, const mt_machine *m, const char *name);

/* The member this machine waits for most per exchange round (false when none). */
bool mt_slowest_wait(const mt_snapshot *s, uint32_t *k, float *us);
/* The section's name in the arena. */
const char *mt_sec_name(mt_frame *f, const mt_section *p);
/* Rect helpers */
static inline tui_rect mt_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) { return (tui_rect){x, y, w, h}; }

#endif
