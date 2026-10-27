/* @ajax/mesh-top-ui-train: mesh-top's Train tab (crates/mesh-top src/ui.rs, "4 train"): one pane
 * per machine with the loss curve on a log scale (braille), the step, rate, loss, params, peer,
 * RTT and the slowest member's wait; under them, whether the lockstep program's params are
 * bitwise identical across every machine at the latest common step. */
#ifndef MESH_TOP_UI_TRAIN_H
#define MESH_TOP_UI_TRAIN_H

#include "mesh/top_ui_kit.h"

/* "train params bitwise identical across N: YES (at step S)". */
tui_line mt_identity_line(mt_frame *f, const mt_app *app);
void mt_draw_train(mt_frame *f, tui_rect area, const mt_app *app);

#endif
