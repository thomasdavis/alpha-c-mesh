/* @ajax/mesh-top-ui-programs: mesh-top's Programs tab (crates/mesh-top src/ui.rs, "2 programs"):
 * one program table per machine (kind, state, placement, nodes, rates, task-time quantiles, the
 * CPU executors' pid, CPU and RSS, PTX hash), the selected program's view pane (counters, manifest
 * fields, a hexdump of its snap bytes), the flame chart of the last 100 ms of trace, and the log. */
#ifndef MESH_TOP_UI_PROGRAMS_H
#define MESH_TOP_UI_PROGRAMS_H

#include "mesh/top_ui_kit.h"

/* The flame chart's window. */
#define MT_FLAME_NS 100000000u

/* A program's placement cell: doom's `place` mode, else the fleet scheduler's (`all`, `0,2`,
 * `pin 1`, `2>1` while it migrates). */
tui_table_cell mt_place_cell(mt_frame *f, const mt_section *p, uint32_t sched);
void mt_draw_programs(mt_frame *f, tui_rect area, const mt_app *app, mt_instant now);

#endif
