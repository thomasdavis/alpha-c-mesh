/* @ajax/mesh-top-ui-fleet: mesh-top's fleet tabs (crates/mesh-top src/ui_fleet.rs and ui.rs's
 * "9 fleet"): Fleet (9), Jobs (0) and Pipes (-), drawn from meshd's views (`view fleet|jobs|pipes`,
 * the same records as http /fleet, /jobs, /pipes), and the fleet from the snapshots alone while
 * the view is not fetched yet. Each degrades by dropping panes; a tiny terminal keeps a one-line
 * summary. */
#ifndef MESH_TOP_UI_FLEET_H
#define MESH_TOP_UI_FLEET_H

#include "mesh/top_ui_kit.h"

/* A bar of w cells for 0..1 (eighths). */
const char *mt_bar(mt_frame *f, double frac, size_t w);
/* At most w characters (… when cut). */
const char *mt_cut(mt_frame *f, const char *s, size_t w);
void mt_draw_fleet(mt_frame *f, tui_rect area, const mt_app *app, mt_instant now);
void mt_draw_jobs(mt_frame *f, tui_rect area, const mt_app *app, mt_instant now);
void mt_draw_pipes(mt_frame *f, tui_rect area, const mt_app *app, mt_instant now);
/* The fleet from the snapshots alone: every machine's view of the leader and the desired state,
 * the member table, the lockstep group and the jobs. */
void mt_draw_fleet_snap(mt_frame *f, tui_rect area, const mt_app *app);

#endif
