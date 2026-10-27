/* @ajax/mesh-top-ui-net: mesh-top's network panes (crates/mesh-top src/ui.rs, "network" and
 * "7 net"): the Net tab (the link matrix, every directed link's frames by kind, every machine's
 * interfaces), a machine panel's Network box and the overview's network cells. */
#ifndef MESH_TOP_UI_NET_H
#define MESH_TOP_UI_NET_H

#include "mesh/top_ui_kit.h"

/* The overview's sparkline width (one column per NIC sample: a second). */
#define MT_OV_SPARK 14

/* The overview's network cells (3, or 4 when wide); returns the count. */
size_t mt_net_cells(mt_frame *f, const mt_machine *m, bool wide, tui_table_cell *out);
/* The Network box's lines for one machine, width columns (compact: one short line per link). */
void mt_net_lines(mt_frame *f, const mt_machine *m, const mt_snapshot *s, uint16_t width, bool compact, mt_lines *out);
void mt_draw_net_box(mt_frame *f, tui_rect area, const mt_machine *m, const mt_lines *lines);
/* The Net tab. */
void mt_draw_net(mt_frame *f, tui_rect area, const mt_app *app);

#endif
