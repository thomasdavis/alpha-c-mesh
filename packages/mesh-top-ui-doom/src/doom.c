/* ui.rs's "3 doom" section, ported. */
#include "mesh/top_ui_doom.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

tui_color mt_wall_color(uint32_t kind, uint32_t side, float dist) {
    const uint8_t *base = MT_WALL_RGB[(size_t)(kind - 1) % 7];
    float fog;
    if (isfinite(dist)) {
        fog = 1.0f / (1.0f + 0.12f * (dist > 0.0f ? dist : 0.0f));
        if (fog < 0.22f) fog = 0.22f;
        if (fog > 1.0f) fog = 1.0f;
    } else {
        fog = 0.22f;
    }
    return mt_rgb(base, fog * (side == 1 ? 0.68f : 1.0f));
}

bool mt_shown_frame(const mt_section *p, mt_shown *out) {
    uint32_t show = MESH_DOOM_D_SHOW;
    uint32_t seq = mt_section_view_u32(p, show + MESH_SH_SEQ);
    if (seq == 0 || mt_section_view_len(p) < MESH_DOOM_SNAP_BYTES) return false;
    uint32_t k = seq & 1, b = mesh_doom_show_data(seq & 1);
    out->arrays[0] = b + MESH_DOOM_SHOW_HEIGHT;
    out->arrays[1] = b + MESH_DOOM_SHOW_KIND;
    out->arrays[2] = b + MESH_DOOM_SHOW_SIDE;
    out->arrays[3] = b + MESH_DOOM_SHOW_DIST;
    out->frame = mt_section_view_u32(p, show + MESH_SH_BUF + MESH_SH_BUF_BYTES * k + MESH_SHB_FRAME);
    out->sys = mt_section_view_u32(p, show + MESH_SH_BUF + MESH_SH_BUF_BYTES * k + MESH_SHB_SYS);
    return true;
}

/* One row under the frame: each column in the colour of the machine that rendered it. */
static void render_markers(mt_frame *f, tui_rect area, uint32_t sys, uint32_t parts) {
    for (uint16_t x = 0; x < area.width; x++) {
        uint32_t c = (uint32_t)x * MESH_DOOM_WIDTH / tui_max16(area.width, 1);
        uint32_t part = c * parts / MESH_DOOM_WIDTH;
        if (part > 7) part = 7;
        uint32_t k = sys >> (4 * part) & 0xf;
        tui_cell *cell = tui_buffer_cell(f->buf, (uint16_t)(area.x + x), area.y);
        if (!cell) continue;
        tui_cell_set_str(cell, "━");
        cell->fg = mt_sys_color(k);
        cell->bg = MT_BG;
    }
}

static const uint8_t SKY_TOP[3] = {70, 80, 120}, SKY_LOW[3] = {16, 18, 30}, FLOOR_NEAR[3] = {30, 24, 18}, FLOOR_FAR[3] = {120, 92, 62};

void mt_render_doom_at(mt_frame *f, tui_rect area, const mt_section *p, const uint32_t at[4]) {
    if (area.width == 0 || area.height == 0) return;
    float rows = mt_section_view_f32(p, MESH_DOOM_D_ROWS);
    if (!(isfinite(rows) && rows >= 1.0f)) rows = 1.0f;
    float px = (float)area.height * 2.0f;
    float half = px / 2.0f;
    for (uint16_t x = 0; x < area.width; x++) {
        size_t c = (size_t)x * MESH_DOOM_WIDTH / area.width;
        float h = mt_section_view_f32(p, at[0] + 4 * c);
        uint32_t kind = mt_section_view_u32(p, at[1] + 4 * c);
        uint32_t side = mt_section_view_u32(p, at[2] + 4 * c);
        float dist = mt_section_view_f32(p, at[3] + 4 * c);
        float wall = 0.0f;
        if (kind != 0 && isfinite(h) && h > 0.0f) {
            wall = h / rows * px;
            if (!(wall <= px * 4.0f)) wall = px * 4.0f; /* f32::min: NaN takes the other */
        }
        float top = half - wall / 2.0f, bot = half + wall / 2.0f;
        tui_color wc = wall > 0.0f ? mt_wall_color(kind, side, dist) : MT_BG;
        for (uint16_t y = 0; y < area.height; y++) {
            tui_color pix[2];
            for (int k = 0; k < 2; k++) {
                float yy = (float)(2 * y + k) + 0.5f;
                if (wall > 0.0f && yy >= top && yy < bot) pix[k] = wc;
                else if (yy < half) pix[k] = mt_lerp(SKY_TOP, SKY_LOW, yy / half);
                else pix[k] = mt_lerp(FLOOR_NEAR, FLOOR_FAR, (yy - half) / half);
            }
            tui_cell *cell = tui_buffer_cell(f->buf, (uint16_t)(area.x + x), (uint16_t)(area.y + y));
            if (!cell) continue;
            tui_cell_set_str(cell, "▀");
            cell->fg = pix[0];
            cell->bg = pix[1];
        }
    }
}

void mt_render_doom(mt_frame *f, tui_rect area, const mt_section *p) {
    const uint32_t at[4] = {MESH_DOOM_D_HEIGHT, MESH_DOOM_D_KIND, MESH_DOOM_D_SIDE, MESH_DOOM_D_DIST};
    mt_render_doom_at(f, area, p, at);
}

/* The 16x16 map, two characters a cell, the player as an arrow. */
static void render_minimap(mt_frame *f, tui_rect area, const mt_section *p) {
    float px = mt_section_view_f32(p, MESH_DOOM_D_POS_X), py = mt_section_view_f32(p, MESH_DOOM_D_POS_Y);
    float dx = mt_section_view_f32(p, MESH_DOOM_D_DIR_X), dy = mt_section_view_f32(p, MESH_DOOM_D_DIR_Y);
    /* y grows downward on screen as it does in the map's rows. */
    static const char *const arrows[8] = {"→", "↘", "↓", "↙", "←", "↖", "↑", "↗"};
    int32_t o = mt_sat_i32(roundf(atan2f(dy, dx) / 0.785398163397448309616f));
    size_t oct = (size_t)(((o % 8) + 8) % 8);
    int32_t fx = mt_sat_i32(floorf(px)), fy = mt_sat_i32(floorf(py));
    for (uint16_t my = 0; my < tui_min16(16, area.height); my++)
        for (uint16_t mx = 0; mx < tui_min16(16, (uint16_t)(area.width / 2)); mx++) {
            uint32_t k = mt_section_view_u32(p, MESH_DOOM_D_MAP + 4 * (16 * (size_t)my + mx));
            const char *sym;
            tui_style st;
            if (fx == mx && fy == my) {
                sym = MT_FMT(f, "%s ", arrows[oct]);
                st = mt_fg_bg_bold(MT_WARN, MT_FAINT);
            } else if (k == 0) {
                sym = "· ";
                st = tui_fg(MT_FAINT);
            } else {
                sym = "██";
                st = tui_fg(mt_wall_color(k, 0, 0.0f));
            }
            tui_buffer_set_string(f->buf, (uint16_t)(area.x + 2 * mx), (uint16_t)(area.y + my), sym, st);
        }
}

static void centered_note(mt_frame *f, tui_rect inner, const char *msg) {
    tui_paragraph p = tui_paragraph_new(tui_text_line(f->a, tui_line_span(f->a, mt_styled(msg, tui_fg_bg(MT_BG, MT_WARN)))));
    p.alignment = TUI_ALIGN_CENTER;
    tui_paragraph_render(f->a, &p, mt_rect(inner.x, inner.y, inner.width, 1), f->buf);
}

static int u32_cmp(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void mt_draw_doom(mt_frame *f, tui_rect area, const mt_app *app) {
    const mt_machine *m = mt_app_focused(app);
    uint16_t side_w = area.width >= 80 ? 36 : 0;
    tui_rect hs[2];
    tui_hsplit(area, 2, (tui_constraint[]){TUI_FILL(1), TUI_LENGTH(side_w)}, hs);
    tui_rect view = hs[0], side = hs[1];
    const mt_section *doom = mt_machine_program(m, "doom");
    mt_place place;
    bool has_place = doom && mt_place_of(doom, &place);
    bool placed = has_place && place.kind != MT_PLACE_LOCAL;
    /* fps: the frames shown when placed (whole frames, matched halves), else doom's "frames" counter. */
    const char *fps_s = "-";
    if (placed) {
        fps_s = mt_ff(f, mt_rate_per_s(&m->doom_shown), 1);
    } else if (doom) {
        for (size_t i = 0; i < MESH_COUNTERS; i++) {
            char cn[3 * MESH_NAME_BYTES + 1];
            mt_section_counter_name(doom, i, cn);
            if (strcmp(cn, "frames") == 0) {
                fps_s = mt_ff(f, mt_machine_counter_rate(m, mt_section_slot(doom), i), 1);
                break;
            }
        }
    }
    uint32_t fanout = 4;
    if (doom) {
        fanout = mt_section_pt(doom, MESH_PT_FANOUT);
        if (fanout < 1) fanout = 1;
    }
    const char *where_s = "";
    if (has_place && place.kind == MT_PLACE_SPLIT) {
        mt_part_run runs[MESH_PLACE_PARTS_MAX];
        size_t nr = mt_part_runs(place.assign, fanout, runs);
        const char *j = "";
        for (size_t i = 0; i < nr; i++) {
            uint32_t c0 = runs[i].first * MESH_DOOM_WIDTH / fanout, c1 = (runs[i].last + 1) * MESH_DOOM_WIDTH / fanout - 1;
            j = MT_FMT(f, "%s%s%u-%u system %u", j, i ? " · " : "", c0, c1, runs[i].system);
        }
        where_s = MT_FMT(f, "  split: cols %s", j);
    } else if (has_place && place.kind == MT_PLACE_PIN) {
        where_s = MT_FMT(f, "  pinned to system %u", place.pin);
    }
    tui_color color;
    tui_line title = mt_machine_title(f, m, true, MT_FMT(f, "doom  %s fps%s", fps_s, where_s), &color);
    tui_block block = mt_panel(f, title, color);
    tui_rect inner = tui_block_inner(&block, view);
    mt_block(f, &block, view);
    const mt_section *p = mt_bound(f, inner, m, "doom");
    if (!p) return;
    mt_shown sf;
    bool shown = placed && mt_shown_frame(p, &sf);
    if (shown) {
        /* The frame, then one row marking which machine rendered each column. */
        tui_rect vs[2];
        tui_vsplit(inner, 2, (tui_constraint[]){TUI_FILL(1), TUI_LENGTH(1)}, vs);
        mt_render_doom_at(f, vs[0], p, sf.arrays);
        uint32_t parts = mt_section_pt(p, MESH_PT_FANOUT);
        render_markers(f, vs[1], sf.sys, parts < 1 ? 1 : parts);
    } else {
        mt_render_doom(f, inner, p);
    }
    uint32_t here = m->has_snap ? mt_snapshot_host_u32(&m->snap, MESH_S_SYSTEM) : 0;
    bool pinned_away = has_place && place.kind == MT_PLACE_PIN && place.pin != here;
    if (!mt_section_running(p) && !pinned_away)
        centered_note(f, inner, MT_FMT(f, " doom is %s: press Esc, then d to start it ", mt_section_state_name(p)));
    else if (placed && !shown)
        centered_note(f, inner, " placed: waiting for the first whole frame ");
    if (side_w == 0) return;
    tui_rect ss[2];
    tui_vsplit(side, 2, (tui_constraint[]){TUI_LENGTH(18), TUI_FILL(1)}, ss);
    tui_block map_block = mt_panel(f, tui_line_span(f->a, mt_label(" map ")), MT_FAINT);
    tui_rect map_inner = tui_block_inner(&map_block, ss[0]);
    mt_block(f, &map_block, ss[0]);
    render_minimap(f, map_inner, p);
    float px = mt_section_view_f32(p, MESH_DOOM_D_POS_X), py = mt_section_view_f32(p, MESH_DOOM_D_POS_Y);
    float dx = mt_section_view_f32(p, MESH_DOOM_D_DIR_X), dy = mt_section_view_f32(p, MESH_DOOM_D_DIR_Y);
    uint32_t me = 0;
    mt_machine_system(m, &me);
    tui_line place_line;
    if (!has_place) {
        place_line = MT_LINE(f, mt_label("place "), mt_fgs("-", MT_FAINT));
    } else if (place.kind == MT_PLACE_LOCAL) {
        place_line = MT_LINE(f, mt_label("place "), mt_val("local"), mt_label("  p/v: place"));
    } else if (place.kind == MT_PLACE_PIN) {
        place_line = MT_LINE(f, mt_label("place "), mt_fgb(MT_FMT(f, "pin %u", place.pin), mt_sys_color(place.pin)),
                             mt_label(place.pin == me ? "  runs here" : "  mirrored here"));
    } else {
        size_t mine = 0;
        for (size_t b = 0; b < fanout && b < MESH_PLACE_PARTS_MAX; b++) mine += place.assign[b] == (uint8_t)me;
        const char *role = place.assign[0] == me ? MT_FMT(f, "  owner (pose), %zu part%s", mine, mine == 1 ? "" : "s")
                           : mine > 0          ? MT_FMT(f, "  follower, %zu part%s", mine, mine == 1 ? "" : "s")
                                               : "  mirror (no parts)";
        place_line = MT_LINE(f, mt_label("place "), mt_fgb("split", MT_ACCENT), mt_label(role));
    }
    uint32_t frame_no = shown ? sf.frame : mt_section_view_u32(p, MESH_DOOM_D_FRAME);
    mt_lines lines = {0};
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("fps   "), mt_fgb(fps_s, MT_WARN)));
    mt_lines_push(f, &lines, place_line);
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("frame "), mt_val(MT_FMT(f, "%u", frame_no)), mt_label(shown ? "  (shown)" : "")));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("pos   "), mt_val(MT_FMT(f, "%s, %s", mt_ff(f, px, 2), mt_ff(f, py, 2)))));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("dir   "), mt_val(MT_FMT(f, "%s, %s", mt_ffp(f, dx, 2), mt_ffp(f, dy, 2)))));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("cols  "), mt_val(MT_FMT(f, "%u", MESH_DOOM_WIDTH)), mt_label(" rays, 1 lane each")));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("slot  "), mt_val(MT_FMT(f, "%u", mt_section_slot(p))),
                                     mt_label(MT_FMT(f, "  nodes %u", mt_section_pt(p, MESH_PT_NODES)))));
    size_t nparts = fanout < MESH_PLACE_PARTS_MAX ? fanout : MESH_PLACE_PARTS_MAX;
    if (has_place && place.kind == MT_PLACE_SPLIT) {
        /* The assignment, one coloured system digit per part. */
        tui_line parts = tui_line_span(f->a, mt_label("parts "));
        for (size_t i = 0; i < nparts; i++) {
            uint8_t k = place.assign[i];
            tui_line_push(f->a, &parts, k == MESH_PART_NONE ? mt_label("- ") : mt_fgb(MT_FMT(f, "%u ", k), mt_sys_color(k)));
        }
        mt_lines_push(f, &lines, parts);
        uint32_t lat = mt_section_view_u32(p, MESH_DOOM_D_SHOW + MESH_SH_LAT_US);
        mt_lines_push(f, &lines, MT_LINE(f, mt_label("added "), mt_val(MT_FMT(f, "%s ms", mt_ff(f, (float)lat / 1000.0f, 2))), mt_label(" (last part)")));
    }
    if (has_place && place.kind != MT_PLACE_LOCAL) {
        uint32_t to = mt_place_input_to(place, me);
        mt_lines_push(f, &lines, MT_LINE(f, mt_label("keys  "), mt_val(to == me ? "here" : MT_FMT(f, "→ system %u", to))));
        /* A legend of the systems whose colours the marker row uses. */
        uint32_t syss[MESH_PLACE_PARTS_MAX + 8];
        size_t ns = 0;
        if (place.kind == MT_PLACE_PIN) {
            syss[ns++] = place.pin;
        } else {
            for (size_t i = 0; i < nparts; i++)
                if (place.assign[i] != MESH_PART_NONE) syss[ns++] = place.assign[i];
        }
        if (shown)
            for (uint32_t b = 0; b < fanout && b < 8; b++) syss[ns++] = sf.sys >> (4 * b) & 0xf;
        qsort(syss, ns, sizeof *syss, u32_cmp);
        tui_line legend = tui_line_empty();
        for (size_t i = 0; i < ns; i++) {
            if (i && syss[i] == syss[i - 1]) continue;
            tui_line_push(f->a, &legend, mt_fgs("━", mt_sys_color(syss[i])));
            tui_line_push(f->a, &legend, mt_label(MT_FMT(f, " system %u  ", syss[i])));
        }
        mt_lines_push(f, &lines, legend);
    }
    tui_block b = mt_panel(f, tui_line_span(f->a, mt_label(" player ")), MT_FAINT);
    tui_paragraph pp = tui_paragraph_new(mt_text(f, &lines));
    pp.block = &b;
    tui_paragraph_render(f->a, &pp, ss[1], f->buf);
}
