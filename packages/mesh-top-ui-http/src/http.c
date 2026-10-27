/* ui.rs's "6 http" section, ported. */
#include "mesh/top_ui_http.h"

#include "tui/utf8.h"

#include <stdlib.h>
#include <string.h>

const char *mt_http_target(mt_frame *f, const mt_machine *m) {
    const char *colon = strrchr(m->endpoint, ':');
    const char *ep_host = colon ? tui_arena_strndup(f->a, m->endpoint, (size_t)(colon - m->endpoint)) : m->endpoint;
    char bound[193] = "";
    if (m->has_snap) mt_snapshot_host_str(&m->snap, MESH_S_HTTP_ADDR, 64, bound);
    const char *c = strrchr(bound, ':');
    if (!c) return MT_FMT(f, "%s:%u", ep_host, MESH_HTTP_PORT);
    size_t il = (size_t)(c - bound);
    if ((il == 7 && memcmp(bound, "0.0.0.0", 7) == 0) || (il == 4 && memcmp(bound, "[::]", 4) == 0)) return MT_FMT(f, "%s:%s", ep_host, c + 1);
    return tui_arena_strdup(f->a, bound);
}

static void count_line(void *ctx, const char *line, size_t len) {
    (void)line;
    (void)len;
    ++*(size_t *)ctx;
}

void mt_draw_http(mt_frame *f, tui_rect area, const mt_app *app) {
    const mt_machine *m = mt_app_focused(app);
    tui_color color;
    tui_line title = mt_machine_title(f, m, true, "http: GET served by the GPU", &color);
    tui_block block = mt_panel(f, title, color);
    tui_rect inner = tui_block_inner(&block, area);
    mt_block(f, &block, area);
    tui_rect r[3];
    tui_vsplit(inner, 3, (tui_constraint[]){TUI_LENGTH(4), TUI_LENGTH(3), TUI_FILL(1)}, r);
    const char *path = *app->http_input ? app->http_input : "/";
    const char *curl = MT_FMT(f, "curl -s http://%s%s", mt_http_target(f, m), path);
    const mt_section *h = mt_machine_program(m, "http");
    mt_lines lines = {0};
    if (m->has_snap && h) {
        const mt_snapshot *s = &m->snap;
        char last[193];
        mt_snapshot_host_str(s, MESH_S_LAST_HTTP, 64, last);
        mt_lines_push(f, &lines,
                      MT_LINE(f, mt_label("served by GPU "), mt_val(MT_FMT(f, "%u", mt_section_view_u32(h, MESH_HTTP_HV_SERVED))),
                              mt_label("   relayed by meshd "), mt_val(MT_FMT(f, "%u", mt_snapshot_host_u32(s, MESH_S_HTTP_REQUESTS))),
                              mt_label("   last code "), mt_val(MT_FMT(f, "%u", mt_section_view_u32(h, MESH_HTTP_HV_LAST_CODE))),
                              mt_label("   http "), mt_styled(mt_section_state_name(h), mt_state_style(h))));
        mt_lines_push(f, &lines, MT_LINE(f, mt_label("last request  "), mt_fgs(tui_arena_strdup(f->a, last), MT_ACCENT)));
        mt_lines_push(f, &lines, MT_LINE(f, mt_label("try           "), mt_fgs(curl, MT_WARN)));
    } else if (m->has_snap) {
        mt_lines_push(f, &lines, tui_line_span(f->a, mt_styled(MT_FMT(f, " http is not loaded on %s ", mt_name(f, m)), mt_fg_bg_bold(MT_BG, MT_WARN))));
    } else {
        mt_lines_push(f, &lines, tui_line_span(f->a, mt_fgs(MT_FMT(f, "no snapshot (%s)", m->status), MT_WARN)));
    }
    mt_par(f, mt_text(f, &lines), r[0]);

    tui_block ib = mt_panel(f, tui_line_span(f->a, mt_label(" path (Enter sends `http <path>`) ")), MT_ACCENT);
    tui_paragraph ip = tui_paragraph_new(
        tui_text_line(f->a, MT_LINE(f, mt_fgs("GET ", MT_DIM), mt_fgb(tui_arena_strdup(f->a, app->http_input), MT_FG), mt_fgs("█", MT_ACCENT))));
    ip.block = &ib;
    tui_paragraph_render(f->a, &ip, r[1], f->buf);

    const char *body;
    if (m->has_http_resp) {
        size_t ln;
        char *lossy = tui_utf8_lossy(m->http_resp, m->http_resp_len, &ln);
        /* .replace("\r\n", "\n") */
        char *o = tui_arena_alloc(f->a, ln + 1);
        size_t k = 0;
        for (size_t i = 0; i < ln; i++) {
            if (lossy[i] == '\r' && i + 1 < ln && lossy[i + 1] == '\n') continue;
            o[k++] = lossy[i];
        }
        o[k] = 0;
        free(lossy);
        body = o;
    } else {
        body = "(no response yet: press Enter)";
    }
    size_t total = 0;
    tui_str_lines(body, count_line, &total);
    tui_block rb = mt_panel(f, tui_line_span(f->a, mt_label(MT_FMT(f, " response  %zu lines  scroll %u ", total, app->http_scroll))), MT_FAINT);
    tui_paragraph rp = tui_paragraph_new(tui_text_from_str(f->a, body));
    rp.wrap = TUI_WRAP_KEEP;
    rp.scroll_y = app->http_scroll;
    rp.block = &rb;
    tui_paragraph_render(f->a, &rp, r[2], f->buf);
}
