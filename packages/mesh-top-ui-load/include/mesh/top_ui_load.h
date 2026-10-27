/* @ajax/mesh-top-ui-load: mesh-top's load panes (crates/mesh-top src/ui.rs, "8 load"): the Load
 * tab (every machine's GPU share and host pressure, the focused machine's cores, programs and
 * share history), the Load box in a machine panel, and the overview's load cells. */
#ifndef MESH_TOP_UI_LOAD_H
#define MESH_TOP_UI_LOAD_H

#include "mesh/top_ui_kit.h"

/* The overview's load figures, from a machine's own sample or a member's F_LOADREP. */
typedef struct {
    mt_opt_f32 share;
    float cpu, big;
    mt_opt_f32 little;
    bool has_psi;
    float psi[2];
    mt_opt_f32 mem_free;
    float meshd;
} mt_load_row;
mt_load_row mt_load_row_from_load(const mt_load *l);
mt_load_row mt_load_row_from_member(const mt_member_load *l);

/* Which load columns the overview has room for. */
typedef enum { MT_LOAD_COLS_NONE, MT_LOAD_COLS_ONE, MT_LOAD_COLS_PAIR, MT_LOAD_COLS_FULL } mt_load_cols;
size_t mt_load_cols_header(mt_load_cols lt, const char **out);     /* at most 4 */
size_t mt_load_cols_widths(mt_load_cols lt, tui_constraint *out);  /* at most 4 */
/* The overview's load cells (r NULL: dashes); returns the count written (at most 4). */
size_t mt_load_cells(mt_frame *f, const mt_load_row *r, mt_load_cols lt, tui_table_cell *out);

/* `88/124G`. */
const char *mt_gb_pair(mt_frame *f, uint32_t a_mb, uint32_t b_mb);
/* The Load box's lines in a machine panel: full (3 lines) or compact (1). */
void mt_load_lines(mt_frame *f, const mt_load *l, bool compact, mt_lines *out);
void mt_draw_load_box(mt_frame *f, tui_rect area, const mt_snapshot *s, const mt_lines *lines);
/* The Load tab. */
void mt_draw_load(mt_frame *f, tui_rect area, const mt_app *app);

#endif
