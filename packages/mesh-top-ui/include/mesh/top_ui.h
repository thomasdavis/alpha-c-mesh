/* @ajax/mesh-top-ui: mesh-top's frame (crates/mesh-top src/ui.rs's draw): the tab bar, the current
 * tab's panes, the key help and last reply in the status bar, and the prompt over the body. Every
 * value comes from a snapshot (or a trace / log / view reply) through mesh-top-snap's accessors;
 * nothing here talks to the network. */
#ifndef MESH_TOP_UI_H
#define MESH_TOP_UI_H

#include "mesh/top_ui_kit.h"

/* Draws one frame of the app into buf (its area is the terminal); a is the frame's arena. */
void mt_ui_draw(tui_arena *a, tui_buffer *buf, const mt_app *app, mt_instant now);
/* What a terminal shows after the frame is flushed onto a blank screen (ratatui's diff onto
 * TestBackend): the cells a wide symbol covers stay blank. out must have frame's area. */
void mt_ui_flush(const tui_buffer *frame, tui_buffer *out);

#endif
