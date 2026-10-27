/* @ajax/mesh-top-ui-mesh: mesh-top's Mesh tab (crates/mesh-top src/ui.rs, "1 mesh"): the cluster
 * overview (one row per system, from its own endpoint or another member's record of it), and one
 * panel per machine: GPU, nodes, blinks, peers, the program chips, the tasks/s sparkline, the
 * Network and Load boxes and the node grid. */
#ifndef MESH_TOP_UI_MESH_H
#define MESH_TOP_UI_MESH_H

#include "mesh/top_ui_kit.h"

/* One overview row: a system (from its own endpoint, or only another member's record of it) or a
 * single machine. */
typedef struct {
    bool has_system;
    uint32_t system;
    bool has_machine;
    size_t machine; /* the endpoint index reporting it */
    bool has_member;
    mt_member member; /* its record as another member sees it (no endpoint of its own) */
} mt_cluster_row;
/* Systems 0..N-1 in order, then machines that are not in a cluster; returns the count (malloc'd). */
size_t mt_cluster_rows(const mt_app *app, mt_cluster_row **out);

/* The gpt2 workers' last request as spans; false without a gpt2 program. */
bool mt_infer_spans(mt_frame *f, const mt_snapshot *s, tui_line *out);

void mt_draw_mesh(mt_frame *f, tui_rect area, const mt_app *app, mt_instant now);

#endif
