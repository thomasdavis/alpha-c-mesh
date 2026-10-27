/* ui.rs's frame: draw, the tab bar, the status bar and the prompt, ported. */
#include "mesh/top_ui.h"

#include "mesh/top_ui_doom.h"
#include "mesh/top_ui_fleet.h"
#include "mesh/top_ui_http.h"
#include "mesh/top_ui_infer.h"
#include "mesh/top_ui_load.h"
#include "mesh/top_ui_mesh.h"
#include "mesh/top_ui_net.h"
#include "mesh/top_ui_programs.h"
#include "mesh/top_ui_render.h"
#include "mesh/top_ui_train.h"
#include "tui/unicode_width.h"

#include <string.h>

static tui_line tabs_line(mt_frame *f, const mt_app *app, bool full, bool brand) {
    tui_line l = tui_line_empty();
    if (brand) {
        tui_line_push(f->a, &l, mt_styled(" mesh-top ", mt_fg_bg_bold(MT_BG, MT_ACCENT)));
        tui_line_push(f->a, &l, tui_span_raw(" "));
    }
    for (int i = 0; i < MT_TABS; i++) {
        tui_style st = i == app->tab ? mt_fg_bg_bold(MT_BG, MT_WARN) : tui_fg(MT_DIM);
        const char *text = full || i == app->tab ? MT_FMT(f, " %c %s ", MT_TAB_KEYS[i], MT_TAB_NAMES[i]) : MT_FMT(f, " %c ", MT_TAB_KEYS[i]);
        tui_line_push(f->a, &l, mt_styled(text, st));
    }
    return l;
}

static void draw_tabs(mt_frame *f, tui_rect area, const mt_app *app) {
    /* Full names, else the current tab's name and the others' keys, else the keys alone. */
    size_t w = area.width;
    tui_line cands[3] = {tabs_line(f, app, true, true), tabs_line(f, app, false, true), tabs_line(f, app, false, false)};
    tui_line left = cands[2];
    for (int i = 0; i < 3; i++)
        if (tui_line_width(&cands[i]) <= w) {
            left = cands[i];
            break;
        }
    size_t used = tui_line_width(&left);
    mt_par_line(f, left, area);
    size_t online = 0;
    for (size_t i = 0; i < app->nmachines; i++) online += app->machines[i].online;
    tui_span focus[3] = {mt_label("focus "), mt_fgb(MT_FMT(f, "[ %s ]", mt_name(f, mt_app_focused(app))), MT_MACHINE[app->focus % 8]),
                         tui_span_raw(" ")};
    /* The tabs keep their room: the online count goes first, then the focus. */
    tui_line right = MT_LINE(f, mt_label(MT_FMT(f, "%zu/%zu online  ", online, app->nmachines)), focus[0], focus[1], focus[2]);
    if (used + tui_line_width(&right) + 1 > area.width) right = tui_line_of(f->a, 3, focus);
    if (used + tui_line_width(&right) < area.width) {
        tui_paragraph p = tui_paragraph_new(tui_text_line(f->a, right));
        p.alignment = TUI_ALIGN_RIGHT;
        tui_paragraph_render(f->a, &p, area, f->buf);
    }
}

typedef struct {
    const char *k, *d;
} keyhelp;

static void draw_status(mt_frame *f, tui_rect area, const mt_app *app) {
    static const keyhelp load[] = {{"type/↑↓", "program dir"}, {"Enter", "load"}, {"Esc", "cancel"}};
    static const keyhelp unload[] = {{"y", "unload"}, {"any key", "cancel"}};
    static const keyhelp doom[] = {{"WASD/←↑↓→", "move/turn"}, {"Q/E", "strafe"}, {"p", "place local/pin k/split"},
                                   {"v", "split/single"},       {"[ ]", "machine"}, {"Esc", "back"}};
    static const keyhelp confirm[] = {{"y", "confirm"}, {"any key", "cancel"}};
    static const keyhelp http[] = {{"type", "path"}, {"Enter", "GET"}, {"↑↓ PgUp/Dn", "scroll"}, {"Tab", "tab"}, {"Esc", "back"}};
    static const keyhelp fleet[] = {{"↑↓←→", "program/member"}, {"c", "cordon"}, {"p/u", "pin/unpin"},
                                    {"z", "sched pause"},       {"g", "reform"}, {"q", "quit"}};
    static const keyhelp jobs[] = {{"↑↓", "job"}, {"x", "cancel job"}, {"0/-", "jobs/pipes"}, {"q", "quit"}};
    static const keyhelp pipes[] = {{"[ ]", "machine"}, {"9/0", "fleet/jobs"}, {"q", "quit"}};
    static const keyhelp rfleet[] = {{"m", "mark tiles by machine"}, {"t", "swap the job's tone map"}, {"[ ]", "assembling machine"}, {"q", "quit"}};
    static const keyhelp render[] = {{"m", "mark tiles by renderer"}, {"t", "swap tone map"}, {"r", "restart"}, {"[ ]", "machine"}, {"q", "quit"}};
    static const keyhelp programs[] = {{"↑↓", "select"}, {"s", "start/stop"}, {"u", "upgrade"}, {"b", "rollback"}, {"x", "unload"},
                                       {"L", "load"},    {"r", "reset"},      {"[ ]", "machine"}, {"q", "quit"}};
    static const keyhelp other[] = {{"1-9 0 - =", "tab"}, {"[ ]", "machine"}, {"d/t/i", "doom/train/infer"}, {"X", "reset train"}, {"q", "quit"}};
#define PICK(x) (keys = (x), nkeys = sizeof(x) / sizeof *(x))
    const keyhelp *keys;
    size_t nkeys;
    int pk = app->prompt.kind;
    if (pk == MT_PROMPT_LOAD) PICK(load);
    else if (pk == MT_PROMPT_UNLOAD) PICK(unload);
    else if (app->tab == MT_TAB_DOOM) PICK(doom);
    else if (pk == MT_PROMPT_CONFIRM) PICK(confirm);
    else if (app->tab == MT_TAB_HTTP) PICK(http);
    else if (app->tab == MT_TAB_FLEET) PICK(fleet);
    else if (app->tab == MT_TAB_JOBS) PICK(jobs);
    else if (app->tab == MT_TAB_PIPES) PICK(pipes);
    else if (app->tab == MT_TAB_RENDER && mt_app_focused(app)->has_fleet) PICK(rfleet);
    else if (app->tab == MT_TAB_RENDER) PICK(render);
    else if (app->tab == MT_TAB_PROGRAMS) PICK(programs);
    else PICK(other);
#undef PICK
    tui_line l = tui_line_empty();
    for (size_t i = 0; i < nkeys; i++) {
        tui_line_push(f->a, &l, mt_fgb(MT_FMT(f, " %s", keys[i].k), MT_ACCENT));
        tui_line_push(f->a, &l, mt_fgs(MT_FMT(f, " %s", keys[i].d), MT_DIM));
    }
    tui_line_push(f->a, &l, mt_fgs("  │ ", MT_FAINT));
    const char *reply = app->last_reply ? app->last_reply : "";
    tui_line_push(f->a, &l, mt_fgs(reply, strstr(reply, "-> err") ? MT_BAD : MT_GOOD));
    tui_paragraph p = tui_paragraph_new(tui_text_line(f->a, l));
    p.style = tui_style_bg(tui_style_new(), TUI_RGB(22, 25, 34));
    tui_paragraph_render(f->a, &p, area, f->buf);
}

static void draw_prompt(mt_frame *f, tui_rect body, const mt_app *app, const mt_prompt *p) {
    uint16_t w = tui_min16(body.width, p->kind == MT_PROMPT_CONFIRM ? 84 : 64);
    const char *title;
    mt_lines lines = {0};
    if (p->kind == MT_PROMPT_UNLOAD) {
        title = " unload ";
        mt_lines_push(f, &lines, MT_LINE(f, mt_label("unload "), mt_val(p->name), mt_label(" on "), mt_val(mt_name(f, &app->machines[p->machine])),
                                         mt_label("?")));
        mt_lines_push(f, &lines, MT_LINE(f, mt_fgb("y", MT_ACCENT), mt_label(" unload   any other key cancels")));
    } else if (p->kind == MT_PROMPT_CONFIRM) {
        title = " confirm ";
        mt_lines_push(f, &lines, MT_LINE(f, mt_val(p->what), mt_label("?")));
        mt_lines_push(f, &lines, MT_LINE(f, mt_label("sends "), mt_fgs(MT_FMT(f, "`%s`", p->line), MT_ACCENT), mt_label(" to "),
                                         mt_val(mt_name(f, &app->machines[p->machine]))));
        mt_lines_push(f, &lines, MT_LINE(f, mt_fgb("y", MT_ACCENT), mt_label(" confirm   any other key cancels")));
    } else {
        title = " load a program ";
        const mt_machine *fm = mt_app_focused(app);
        mt_lines_push(f, &lines, MT_LINE(f, mt_label("load "), mt_fgb(tui_arena_strdup(f->a, p->input), MT_FG), mt_fgs("█", MT_ACCENT)));
        mt_lines_push(f, &lines, tui_line_span(f->a, mt_label(MT_FMT(f, "catalog (%zu), ↑↓ to pick:", fm->ncatalog))));
        if (fm->ncatalog == 0) mt_lines_push(f, &lines, tui_line_span(f->a, mt_label("  (none yet: waiting for `catalog`)")));
        for (size_t i = 0; i < fm->ncatalog; i++) {
            const char *c = fm->catalog[i];
            const char *slash = strrchr(c, '/');
            bool loaded = mt_machine_program(fm, slash ? slash + 1 : c) != NULL;
            tui_style st = p->has_pick && p->pick == i ? mt_fg_bg_bold(MT_BG, MT_ACCENT) : tui_fg(MT_FG);
            tui_line l = tui_line_span(f->a, mt_styled(MT_FMT(f, "  %s ", c), st));
            if (loaded) tui_line_push(f->a, &l, mt_label(" loaded"));
            mt_lines_push(f, &lines, l);
        }
    }
    uint16_t h = tui_min16((uint16_t)(lines.n + 2), body.height);
    tui_rect r = mt_rect((uint16_t)(body.x + (body.width - w) / 2), (uint16_t)(body.y + (body.height - h) / 2), w, h);
    tui_buffer_clear(f->buf, r);
    tui_block b = mt_panel(f, tui_line_span(f->a, mt_label(title)), MT_WARN);
    tui_paragraph pp = tui_paragraph_new(mt_text(f, &lines));
    pp.wrap = TUI_WRAP_TRIM;
    pp.block = &b;
    tui_paragraph_render(f->a, &pp, r, f->buf);
}

void mt_ui_draw(tui_arena *a, tui_buffer *buf, const mt_app *app, mt_instant now) {
    mt_frame fr = {a, buf};
    mt_frame *f = &fr;
    tui_rect area = buf->area;
    tui_buffer_set_style(buf, area, tui_fg_bg(MT_FG, MT_BG));
    tui_rect r[3];
    tui_vsplit(area, 3, (tui_constraint[]){TUI_LENGTH(1), TUI_FILL(1), TUI_LENGTH(1)}, r);
    draw_tabs(f, r[0], app);
    tui_rect body = r[1];
    switch (app->tab) {
    case MT_TAB_MESH: mt_draw_mesh(f, body, app, now); break;
    case MT_TAB_PROGRAMS: mt_draw_programs(f, body, app, now); break;
    case MT_TAB_DOOM: mt_draw_doom(f, body, app); break;
    case MT_TAB_TRAIN: mt_draw_train(f, body, app); break;
    case MT_TAB_INFER: mt_draw_infer(f, body, app); break;
    case MT_TAB_HTTP: mt_draw_http(f, body, app); break;
    case MT_TAB_LOAD: mt_draw_load(f, body, app); break;
    case MT_TAB_FLEET: mt_draw_fleet(f, body, app, now); break;
    case MT_TAB_JOBS: mt_draw_jobs(f, body, app, now); break;
    case MT_TAB_PIPES: mt_draw_pipes(f, body, app, now); break;
    case MT_TAB_RENDER: mt_draw_render(f, body, app); break;
    default: mt_draw_net(f, body, app); break;
    }
    draw_status(f, r[2], app);
    if (app->prompt.kind != MT_PROMPT_NONE) draw_prompt(f, body, app, &app->prompt);
}

static bool cell_eq(const tui_cell *a, const tui_cell *b) {
    return strcmp(tui_cell_symbol(a), tui_cell_symbol(b)) == 0 && tui_color_eq(a->fg, b->fg) && tui_color_eq(a->bg, b->bg) && a->mod == b->mod;
}

/* tui-render's BufferDiff (ratatui 0.30) against a blank screen, applied to out. */
void mt_ui_flush(const tui_buffer *frame, tui_buffer *out) {
    size_t len = (size_t)frame->area.width * frame->area.height;
    for (size_t i = 0; i < len;) {
        const tui_cell *cur = &frame->cells[i];
        const char *sym = tui_cell_symbol(cur);
        size_t width = tui_symbol_width(sym, strlen(sym));
        if (cell_eq(cur, &TUI_CELL_EMPTY)) {
            i += width > 1 ? width : 1;
            continue;
        }
        out->cells[i] = *cur;
        if (width > 1 && strstr(sym, "\xEF\xB8\x8F")) {
            /* The cells under it are written when their symbols changed. */
            size_t end = i + width < len ? i + width : len;
            for (size_t j = i + 1; j < end;) {
                const char *js = tui_cell_symbol(&frame->cells[j]);
                size_t jw = tui_symbol_width(js, strlen(js));
                if (strcmp(js, " ") != 0) out->cells[j] = frame->cells[j];
                j += jw ? jw : 1;
                if (j > end) end = j < len ? j : len;
            }
            i = end;
        } else {
            i += width > 1 ? width : 1;
        }
    }
}
