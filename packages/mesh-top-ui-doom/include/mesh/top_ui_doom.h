/* @ajax/mesh-top-ui-doom: mesh-top's Doom tab (crates/mesh-top src/ui.rs, "3 doom"): the
 * raycaster's frame in half blocks (two pixels a cell, fogged walls), the placement's shown frame
 * with a row marking which machine rendered each column, the 16x16 minimap and the player pane. */
#ifndef MESH_TOP_UI_DOOM_H
#define MESH_TOP_UI_DOOM_H

#include "mesh/top_ui_kit.h"

/* The frame meshd's placement shows (the show region's current buffer): its column arrays' view
 * offsets, its frame number and which system rendered each part (4 bits per part). */
typedef struct {
    uint32_t arrays[4];
    uint32_t frame, sys;
} mt_shown;
bool mt_shown_frame(const mt_section *p, mt_shown *out);
tui_color mt_wall_color(uint32_t kind, uint32_t side, float dist);
/* The frame: doom's columns stretched to the pane, from column arrays at `at` (heights, kinds,
 * sides, dists). */
void mt_render_doom_at(mt_frame *f, tui_rect area, const mt_section *p, const uint32_t at[4]);
void mt_render_doom(mt_frame *f, tui_rect area, const mt_section *p);
void mt_draw_doom(mt_frame *f, tui_rect area, const mt_app *app);

#endif
