/* @ajax/mesh-top-ui-infer: mesh-top's Infer tab (crates/mesh-top src/ui.rs, "5 infer"): the
 * model's curve on every machine against the target sin(pi x) samples train fits, and the params
 * infer's last run read. */
#ifndef MESH_TOP_UI_INFER_H
#define MESH_TOP_UI_INFER_H

#include "mesh/top_ui_kit.h"

void mt_draw_infer(mt_frame *f, tui_rect area, const mt_app *app);

#endif
