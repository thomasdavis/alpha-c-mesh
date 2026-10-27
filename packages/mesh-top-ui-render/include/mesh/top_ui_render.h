/* @ajax/mesh-top-ui-render: mesh-top's Render tab (crates/mesh-top src/ui_render.rs):
 * programs/pathtrace's image in half-block truecolor, box-filtered to the pane. Fleet mode shows the
 * latest pathtrace job's image as the focused member assembles it (tiles tinted by the machine whose
 * pass was folded last, a per-machine progress bar); otherwise one meshd's pathtrace view, tiles
 * tinted by who rendered them (a member, else the executor and node). */
#ifndef MESH_TOP_UI_RENDER_H
#define MESH_TOP_UI_RENDER_H

#include "mesh/top_ui_kit.h"

/* A bright colour around the wheel at h turns. */
tui_color mt_hue(float h);
void mt_draw_render(mt_frame *f, tui_rect area, const mt_app *app);

#endif
