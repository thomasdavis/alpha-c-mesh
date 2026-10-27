/* @ajax/mesh-top-ui-http: mesh-top's HTTP tab (crates/mesh-top src/ui.rs, "6 http"): what the GPU
 * served and meshd relayed, a curl line for the machine's bound HTTP address, the path input and
 * the last response, wrapped and scrollable. */
#ifndef MESH_TOP_UI_HTTP_H
#define MESH_TOP_UI_HTTP_H

#include "mesh/top_ui_kit.h"

/* Where to curl this machine: meshd's bound HTTP address, an unspecified IP replaced by the
 * endpoint's host; without a snapshot, the endpoint's host and the default HTTP port. */
const char *mt_http_target(mt_frame *f, const mt_machine *m);
void mt_draw_http(mt_frame *f, tui_rect area, const mt_app *app);

#endif
