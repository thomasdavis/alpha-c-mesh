/* ui.rs's "1 mesh" section, ported. */
#include "mesh/top_ui_mesh.h"

#include "mesh/top_ui_load.h"
#include "mesh/top_ui_net.h"
#include "tui/sparkline.h"

#include <stdlib.h>
#include <string.h>

static unsigned popcount(uint32_t x) {
    unsigned n = 0;
    for (; x; x &= x - 1) n++;
    return n;
}

size_t mt_cluster_rows(const mt_app *app, mt_cluster_row **out) {
    size_t cap = app->nmachines + MESH_MAX_SYSTEMS + 1, n = 0;
    mt_cluster_row *r = calloc(cap, sizeof *r);
    if (!r) abort();
    bool clustered = false;
    for (size_t i = 0; i < app->nmachines; i++) {
        uint32_t k;
        clustered |= mt_machine_system(&app->machines[i], &k);
    }
    if (clustered) {
        uint32_t size = mt_app_cluster_size(app);
        for (uint32_t k = 0; k < size; k++) {
            if (n == cap) break;
            bool found = false;
            for (size_t i = 0; i < app->nmachines && !found; i++) {
                uint32_t sk;
                if (mt_machine_system(&app->machines[i], &sk) && sk == k) {
                    r[n++] = (mt_cluster_row){.has_system = true, .system = k, .has_machine = true, .machine = i};
                    found = true;
                }
            }
            if (found) continue;
            mt_cluster_row row = {.has_system = true, .system = k};
            for (size_t i = 0; i < app->nmachines; i++) {
                const mt_machine *m = &app->machines[i];
                uint32_t sk;
                if (!m->has_snap || !mt_snapshot_system(&m->snap, &sk) || !(k < mt_snapshot_systems(&m->snap))) continue;
                mt_member mb = mt_snapshot_member(&m->snap, k);
                /* max_by_key (state, gpu known): the last of equal maxima wins */
                bool better = !row.has_member || mb.state > row.member.state ||
                              (mb.state == row.member.state && (*mb.gpu != 0) >= (*row.member.gpu != 0));
                if (better) {
                    row.has_member = true;
                    row.member = mb;
                }
            }
            r[n++] = row;
        }
    }
    for (size_t i = 0; i < app->nmachines && n < cap; i++) {
        uint32_t sk;
        if (!mt_machine_system(&app->machines[i], &sk) || !clustered)
            r[n++] = (mt_cluster_row){.has_machine = true, .machine = i};
    }
    *out = r;
    return n;
}

static tui_table_cell dash(mt_frame *f) { return mt_cell_span(f, mt_fgs("-", MT_FAINT)); }

static void draw_overview(mt_frame *f, tui_rect area, const mt_app *app, const mt_cluster_row *rows, size_t nrows) {
    uint32_t n = mt_app_cluster_size(app);
    tui_line title = MT_LINE(f, mt_fgb(" cluster ", MT_FG), mt_label(MT_FMT(f, "%u system%s  ", n, n == 1 ? "" : "s")));
    tui_block block = mt_panel(f, title, MT_FAINT);
    bool wide = area.width >= 140;
    /* The load columns: none below 100 columns, `GPU CPU` (and meshd when wide), all four from 230. */
    mt_load_cols lt = area.width >= 230 ? MT_LOAD_COLS_FULL : wide ? MT_LOAD_COLS_PAIR : area.width >= 100 ? MT_LOAD_COLS_ONE : MT_LOAD_COLS_NONE;
    const char *header[32];
    size_t nh = 0;
    const char *h0[5] = {"sys", "host", "GPU", "cc", "SMs"};
    for (size_t i = 0; i < 5; i++) header[nh++] = h0[i];
    if (wide) {
        header[nh++] = "arch";
        header[nh++] = "blk/SM";
    }
    /* Narrow: the linked count joins the peer cell, the step column goes. */
    if (wide) {
        header[nh++] = "peer";
        header[nh++] = "linked";
        header[nh++] = "step";
        header[nh++] = "steps/s";
    } else {
        header[nh++] = "peer";
        header[nh++] = "steps/s";
    }
    if (wide) header[nh++] = "RTT";
    header[nh++] = "tasks/s";
    nh += mt_load_cols_header(lt, header + nh);
    if (wide) {
        header[nh++] = "net rx";
        header[nh++] = "net tx";
        header[nh++] = "mesh";
        header[nh++] = "net, 1 s/col";
    } else {
        header[nh++] = "net ↓ ↑";
        header[nh++] = "mesh";
    }
    header[nh++] = "wait per round, by member";
    tui_constraint widths[32];
    size_t nw = 0;
    widths[nw++] = TUI_LENGTH(4);
    widths[nw++] = TUI_LENGTH(wide ? 14 : 10);
    widths[nw++] = TUI_LENGTH(wide ? 16 : 10);
    widths[nw++] = TUI_LENGTH(4);
    widths[nw++] = TUI_LENGTH(6);
    if (wide) {
        widths[nw++] = TUI_LENGTH(8);
        widths[nw++] = TUI_LENGTH(6);
        widths[nw++] = TUI_LENGTH(8);
        widths[nw++] = TUI_LENGTH(6);
        widths[nw++] = TUI_LENGTH(9);
        widths[nw++] = TUI_LENGTH(7);
        widths[nw++] = TUI_LENGTH(8);
    } else {
        widths[nw++] = TUI_LENGTH(10);
        widths[nw++] = TUI_LENGTH(7);
    }
    widths[nw++] = TUI_LENGTH(8);
    nw += mt_load_cols_widths(lt, widths + nw);
    if (wide) {
        widths[nw++] = TUI_LENGTH(10);
        widths[nw++] = TUI_LENGTH(10);
        widths[nw++] = TUI_LENGTH(4);
        widths[nw++] = TUI_LENGTH(MT_OV_SPARK);
    } else {
        widths[nw++] = TUI_LENGTH(12);
        widths[nw++] = TUI_LENGTH(4);
    }
    widths[nw++] = TUI_FILL(1);
    mt_rows trows = {0};
    for (size_t ri = 0; ri < nrows; ri++) {
        const mt_cluster_row *r = &rows[ri];
        bool focused = r->has_machine && r->machine == app->focus;
        mt_cells cells = {0};
        mt_cells_push(f, &cells,
                      mt_cell_line(f, MT_LINE(f, mt_fgs(focused ? "◆" : " ", MT_ACCENT),
                                              r->has_system ? mt_fgb(MT_FMT(f, "%u", r->system), mt_sys_color(r->system)) : mt_fgs("-", MT_DIM))));
        if (r->has_machine) {
            const mt_machine *m = &app->machines[r->machine];
            if (!m->has_snap) {
                mt_cells_push(f, &cells, mt_cell_str(f, m->endpoint));
                mt_cells_push(f, &cells, mt_cell_span(f, mt_fgs(m->status, MT_BAD)));
                mt_rows_push(f, &trows, mt_row(f, cells.c, cells.n));
                continue;
            }
            const mt_snapshot *s = &m->snap;
            mt_cells_push(f, &cells, mt_cell_span(f, mt_fgb(mt_name(f, m), m->online ? MT_FG : MT_BAD)));
            char gpu[97], lab[32];
            mt_snapshot_gpu(s, gpu);
            mt_cells_push(f, &cells, mt_cell_str(f, mt_short_gpu(f, gpu)));
            mt_gpu_label(mt_snapshot_head(s, MESH_H_CC), mt_snapshot_head(s, MESH_H_BACKEND), lab);
            mt_cells_push(f, &cells, mt_cell_str(f, tui_arena_strdup(f->a, lab)));
            mt_cells_push(f, &cells, mt_cell_str(f, MT_FMT(f, "%u/%u", mt_snapshot_host_u32(s, MESH_S_SMS), mt_snapshot_host_u32(s, MESH_S_SMS_TOTAL))));
            if (wide) {
                char arch[49];
                mt_snapshot_host_str(s, MESH_S_ARCH, 16, arch);
                mt_cells_push(f, &cells, mt_cell_str(f, *arch ? tui_arena_strdup(f->a, arch) : "-"));
                mt_blocks_label(mt_snapshot_head(s, MESH_H_BLOCKS_PER_SM), mt_snapshot_head(s, MESH_H_BACKEND), lab);
                mt_cells_push(f, &cells, mt_cell_str(f, tui_arena_strdup(f->a, lab)));
            }
            uint32_t ps = mt_snapshot_host_u32(s, MESH_S_PEER_STATE);
            tui_span peer = ps == 0   ? mt_fgs("single", MT_DIM)
                            : ps == 1 ? mt_fgb("waiting", MT_WARN)
                            : ps == 2 ? mt_fgb("up", MT_GOOD)
                                      : mt_fgs(MT_FMT(f, "?%u", ps), MT_BAD);
            unsigned linked = popcount(mt_snapshot_host_u32(s, MESH_S_LINKED));
            uint32_t nn = mt_snapshot_systems(s);
            if (nn < 1) nn = 1;
            uint32_t me;
            bool has_me = mt_snapshot_system(s, &me);
            tui_color lc = !has_me ? MT_DIM : linked >= nn ? MT_GOOD : MT_WARN;
            tui_span link = mt_fgs(has_me ? MT_FMT(f, "%u/%u", linked, nn) : "-", lc);
            if (wide) {
                mt_cells_push(f, &cells, mt_cell_span(f, peer));
                mt_cells_push(f, &cells, mt_cell_span(f, link));
            } else if (has_me) {
                mt_cells_push(f, &cells, mt_cell_line(f, MT_LINE(f, peer, tui_span_raw(" "), link)));
            } else {
                mt_cells_push(f, &cells, mt_cell_span(f, peer));
            }
            const mt_section *t = mt_machine_program(m, "train");
            if (t) {
                if (wide) mt_cells_push(f, &cells, mt_cell_str(f, MT_FMT(f, "%llu", (unsigned long long)mt_train_next_step(t))));
                mt_cells_push(f, &cells, mt_cell_span(f, mt_fgs(mt_ff(f, mt_rate_per_s(&m->train_steps), 0), MT_WARN)));
            } else if (wide) {
                mt_cells_push(f, &cells, dash(f));
                mt_cells_push(f, &cells, dash(f));
            } else {
                mt_cells_push(f, &cells, dash(f));
            }
            if (wide) {
                float rtt = mt_snapshot_host_f32(s, MESH_S_PEER_RTT_US);
                mt_cells_push(f, &cells, mt_cell_str(f, rtt > 0.0f ? MT_FMT(f, "%sus", mt_ff(f, rtt, 0)) : "-"));
            }
            mt_cells_push(f, &cells, mt_cell_str(f, mt_fmt_rate(f, mt_snapshot_host_f32(s, MESH_S_TASKS_PER_S))));
            tui_table_cell lc4[4];
            mt_load l;
            mt_load_row lr;
            bool has_l = mt_snapshot_load(s, &l);
            if (has_l) lr = mt_load_row_from_load(&l);
            size_t nl = mt_load_cells(f, has_l ? &lr : NULL, lt, lc4);
            for (size_t i = 0; i < nl; i++) mt_cells_push(f, &cells, lc4[i]);
            tui_table_cell nc[4];
            size_t nnc = mt_net_cells(f, m, wide, nc);
            for (size_t i = 0; i < nnc; i++) mt_cells_push(f, &cells, nc[i]);
            /* The wait it sees for each other member; the longest (the one it waits for) highlighted. */
            uint32_t slow_k;
            float slow_us;
            bool has_slow = mt_slowest_wait(s, &slow_k, &slow_us) && slow_us > 0.0f;
            mt_member ms[MESH_MAX_SYSTEMS];
            size_t nm = mt_snapshot_members(s, ms);
            tui_line spans = tui_line_empty();
            for (size_t i = 0; i < nm; i++) {
                const mt_member *mb = &ms[i];
                if (has_me && mb->system == me) continue;
                tui_style st = mb->state != MESH_MS_UP                 ? tui_fg(MT_BAD)
                               : has_slow && mb->system == slow_k ? tui_fg_bold(MT_WARN)
                                                                   : tui_fg(MT_FG);
                tui_line_push(f->a, &spans, mt_fgs(MT_FMT(f, "%u:", mb->system), mt_sys_color(mb->system)));
                const char *v = mb->state == MESH_MS_ABSENT ? "absent" : mb->state != MESH_MS_UP ? "linked" : mt_fmt_us(f, mb->wait_us);
                tui_line_push(f->a, &spans, mt_styled(MT_FMT(f, "%s ", v), st));
            }
            mt_cells_push(f, &cells, spans.n ? mt_cell_line(f, spans) : dash(f));
        } else if (r->has_member) {
            const mt_member *mb = &r->member;
            /* Known only through another member's record: what it told at JOIN. */
            char lab[32];
            mt_cells_push(f, &cells, mt_cell_span(f, mt_fgs(*mb->host ? tui_arena_strdup(f->a, mb->host) : "?", MT_DIM)));
            mt_cells_push(f, &cells, mt_cell_span(f, mt_fgs(mt_short_gpu(f, mb->gpu), MT_DIM)));
            mt_gpu_label(mb->cc, mb->backend, lab);
            mt_cells_push(f, &cells, mt_cell_str(f, tui_arena_strdup(f->a, lab)));
            mt_cells_push(f, &cells, mt_cell_str(f, MT_FMT(f, "%u/%u", mb->sms, mb->sms_total)));
            if (wide) {
                mt_cells_push(f, &cells, mt_cell_str(f, *mb->arch ? tui_arena_strdup(f->a, mb->arch) : "-"));
                mt_blocks_label(mb->blocks_per_sm, mb->backend, lab);
                mt_cells_push(f, &cells, mt_cell_str(f, tui_arena_strdup(f->a, lab)));
            }
            const char *st = mb->state < MESH_MEMBER_STATES_LEN ? MESH_MEMBER_STATES[mb->state] : "?";
            mt_cells_push(f, &cells, mt_cell_span(f, mt_fgs(st, mb->state == MESH_MS_UP ? MT_GOOD : MT_BAD)));
            if (wide) mt_cells_push(f, &cells, dash(f));
            /* Steps/s, RTT, tasks/s: not reported by peers; its load is (F_LOADREP). */
            if (wide) {
                mt_cells_push(f, &cells, dash(f));
                mt_cells_push(f, &cells, dash(f));
            }
            mt_cells_push(f, &cells, dash(f));
            mt_cells_push(f, &cells, dash(f));
            bool has_ml = false;
            mt_member_load ml = {0};
            for (size_t i = 0; i < app->nmachines; i++) {
                const mt_machine *m = &app->machines[i];
                uint32_t sk;
                mt_member_load x;
                if (!m->has_snap || !mt_snapshot_system(&m->snap, &sk) || !mt_snapshot_member_load(&m->snap, mb->system, &x)) continue;
                if (!has_ml || x.age_ms < ml.age_ms) { /* min_by_key: the first of equal minima */
                    ml = x;
                    has_ml = true;
                }
            }
            mt_load_row lr;
            if (has_ml) lr = mt_load_row_from_member(&ml);
            tui_table_cell lc4[4];
            size_t nl = mt_load_cells(f, has_ml ? &lr : NULL, lt, lc4);
            for (size_t i = 0; i < nl; i++) mt_cells_push(f, &cells, lc4[i]);
            mt_cells_push(f, &cells, mt_cell_span(f, mt_fgs("via peers", MT_DIM)));
        } else {
            mt_cells_push(f, &cells, mt_cell_span(f, mt_fgs("unknown", MT_DIM)));
        }
        mt_rows_push(f, &trows, mt_row(f, cells.c, cells.n));
    }
    tui_table_row hdr = mt_header(f, header, nh);
    tui_table t = tui_table_new(trows.rows, trows.n, widths, nw);
    t.header = &hdr;
    t.column_spacing = 1;
    t.block = &block;
    mt_table(f, &t, area);
}

bool mt_infer_spans(mt_frame *f, const mt_snapshot *s, tui_line *out) {
    static const char *const MODES[4] = {"fast", "fast16", "exact", "exact-cpu"};
    tui_line o = tui_line_empty();
    for (size_t i = 0; i < s->nsections; i++) {
        const mt_section *p = &s->sections[i];
        const char *name = mt_sec_name(f, p);
        if (strncmp(name, "gpt2", 4) != 0 || strcmp(name, "gpt2x") == 0) continue;
        if (mt_section_view_len(p) < 0x44) continue;
        uint32_t role = mt_section_view_u32(p, 0x04), req = mt_section_view_u32(p, 0x08), mi = mt_section_view_u32(p, 0x34);
        const char *mode = mi < 4 ? MODES[mi] : "?";
        float ttft = mt_section_view_f32(p, 0x10), pre = mt_section_view_f32(p, 0x14), tps = mt_section_view_f32(p, 0x18);
        float kv_ms = mt_section_view_f32(p, 0x1c), kv_mb = mt_section_view_f32(p, 0x24), gbps = mt_section_view_f32(p, 0x40);
        uint32_t peer = mt_section_view_u32(p, 0x30);
        tui_line_push(f->a, &o, mt_fgs(MT_FMT(f, "%s ", name), MT_ACCENT));
        tui_line_push(f->a, &o, mt_label(MT_FMT(f, "%u req ", req)));
        if (req == 0) continue;
        tui_line_push(f->a, &o, mt_label(MT_FMT(f, "%s ", mode)));
        if (role == 1) {
            tui_line_push(f->a, &o, mt_label("prefill "));
            tui_line_push(f->a, &o, mt_val(MT_FMT(f, "%sms", mt_ff(f, pre, 1))));
            tui_line_push(f->a, &o, mt_label(" KV out "));
            tui_line_push(f->a, &o, mt_val(MT_FMT(f, "%sMB  ", mt_ff(f, kv_mb, 1))));
        } else if (role == 2) {
            tui_line_push(f->a, &o, mt_label("ttft "));
            tui_line_push(f->a, &o, mt_val(MT_FMT(f, "%sms ", mt_ff(f, ttft, 1))));
            tui_line_push(f->a, &o, mt_val(mt_ff(f, tps, 0)));
            tui_line_push(f->a, &o, mt_label(" tok/s KV "));
            tui_line_push(f->a, &o, mt_val(MT_FMT(f, "%sMB/%sms %sGb/s", mt_ff(f, kv_mb, 1), mt_ff(f, kv_ms, 1), mt_ff(f, gbps, 1))));
            if (peer != UINT32_MAX) {
                tui_line_push(f->a, &o, mt_label(" from "));
                tui_line_push(f->a, &o, mt_fgs(MT_FMT(f, "system %u  ", peer), mt_sys_color(peer)));
            }
        } else {
            tui_line_push(f->a, &o, mt_label("ttft "));
            tui_line_push(f->a, &o, mt_val(MT_FMT(f, "%sms ", mt_ff(f, ttft, 1))));
            tui_line_push(f->a, &o, mt_val(mt_ff(f, tps, 0)));
            tui_line_push(f->a, &o, mt_label(" tok/s  "));
        }
    }
    if (!o.n) return false;
    *out = o;
    return true;
}

static uint16_t div_ceil16(uint16_t a, uint16_t b) { return (uint16_t)(a / b + (a % b != 0)); }

/* One cell per node of every program, labelled with its %smid: hue = the program, brightness =
 * utilisation, red = a running program's heartbeat stalled. */
static void draw_sm_grid(mt_frame *f, tui_rect area, const mt_machine *m, const mt_snapshot *s, mt_instant now) {
    if (area.height < 2 || area.width == 0) return;
    tui_line head = tui_line_span(f->a, mt_label("nodes by program (%smid)"));
    tui_buffer_set_line(f->buf, area.x, area.y, &head, area.width);
    tui_rect cells = {area.x, (uint16_t)(area.y + 1), area.width, (uint16_t)(area.height - 1)};
    uint16_t legend_y = (uint16_t)(cells.y + cells.height - 1);
    size_t total = 0;
    for (size_t i = 0; i < s->nsections; i++) total += mt_section_nodes(&s->sections[i]);
    uint16_t n = (uint16_t)total;
    uint16_t grid_h = tui_sat_sub(cells.height, 1);
    uint16_t per_row = div_ceil16(n, tui_max16(grid_h, 1));
    per_row = tui_max16(per_row, 8);
    per_row = tui_min16(per_row, (uint16_t)(cells.width / 4));
    per_row = tui_max16(per_row, 1);
    uint16_t w = (uint16_t)(cells.width / per_row);
    uint16_t h = (uint16_t)(grid_h / tui_max16(div_ceil16(n, per_row), 1));
    if (h < 1) h = 1;
    if (h > 4) h = 4;
    uint16_t text_h = h >= 3 ? (uint16_t)(h - 1) : h;
    uint16_t i = 0;
    for (size_t pi = 0; pi < s->nsections; pi++) {
        const mt_section *p = &s->sections[pi];
        const char *name = mt_sec_name(f, p);
        size_t nodes = mt_section_nodes(p);
        for (uint32_t node = 0; node < nodes; node++, i++) {
            uint16_t cx = (uint16_t)(cells.x + (i % per_row) * w), cy = (uint16_t)(cells.y + (i / per_row) * h);
            if (cy + text_h > cells.y + grid_h) goto legend;
            mt_node rec = mt_section_node(p, node);
            const mt_node_track *t = mt_machine_node(m, mt_section_slot(p), node);
            float util = t ? t->util : 0.0f;
            bool stalled = mt_section_running(p) && mt_machine_stalled(m, mt_section_slot(p), node, now);
            tui_style style;
            if (stalled) {
                style = mt_fg_bg_bold(TUI_WHITE, MT_BAD);
            } else if (!mt_section_running(p) || rec.tasks == 0) {
                style = tui_fg_bg(MT_DIM, MT_FAINT);
            } else {
                float b = 0.22f + 0.78f * util;
                style = tui_fg_bg(b > 0.55f ? TUI_BLACK : TUI_WHITE, mt_prog_color(name, mt_section_slot(p), b));
            }
            size_t cw = tui_sat_sub(w, 1);
            if (cw < 1) cw = 1;
            const char *lines[3] = {
                cw >= 6 ? MT_FMT(f, "sm %u", rec.sm) : MT_FMT(f, "%u", rec.sm),
                MT_FMT(f, "%s%%", mt_ff(f, util * 100.0f, 0)),
                stalled ? "STALL" : name,
            };
            for (uint16_t dy = 0; dy < text_h && dy < 3; dy++) {
                const char *txt = mt_pad(f, lines[dy], (int)cw, '^');
                tui_buffer_set_stringn(f->buf, cx, (uint16_t)(cy + dy), txt, strlen(txt), cw, style);
            }
        }
    }
legend:;
    tui_line legend = tui_line_empty();
    for (size_t pi = 0; pi < s->nsections; pi++) {
        const mt_section *p = &s->sections[pi];
        const char *name = mt_sec_name(f, p);
        tui_line_push(f->a, &legend, mt_fgs("■", mt_prog_color(name, mt_section_slot(p), 1.0f)));
        tui_line_push(f->a, &legend, mt_label(MT_FMT(f, "%s ", name)));
    }
    tui_line_push(f->a, &legend, mt_fgs("■", MT_BAD));
    tui_line_push(f->a, &legend, mt_label("stalled  bright=busy"));
    tui_buffer_set_line(f->buf, cells.x, legend_y, &legend, cells.width);
}

/* `metal apple9` for the GPU line. */
static const char *mesh_host_label(mt_frame *f, uint32_t word) {
    uint32_t k = mesh_backend_kind(word);
    const char *name = k < 4 ? MESH_BACKENDS[k] : "?";
    return mesh_backend_family(word) > 0 ? MT_FMT(f, "%s apple%u", name, mesh_backend_family(word)) : name;
}

static void draw_machine(mt_frame *f, tui_rect area, const mt_machine *m, bool focused, mt_instant now) {
    tui_color color;
    tui_line title = mt_machine_title(f, m, focused, m->endpoint, &color);
    tui_block block = mt_panel(f, title, color);
    tui_rect inner = tui_block_inner(&block, area);
    mt_block(f, &block, area);
    if (!m->has_snap) {
        mt_no_data(f, inner, m);
        return;
    }
    const mt_snapshot *s = &m->snap;
    /* Below the info lines: the Network box (full, else compact), the tasks/s sparkline (4 rows,
     * else 2) and the node grid (at least 3 rows when it can). */
    mt_lines net_full = {0}, net_compact = {0};
    mt_net_lines(f, m, s, tui_sat_sub(inner.width, 2), false, &net_full);
    mt_net_lines(f, m, s, tui_sat_sub(inner.width, 2), true, &net_compact);
    uint16_t full_h = (uint16_t)(net_full.n + 2), compact_h = (uint16_t)(net_compact.n + 2);
    /* The Load box: 3 lines, else 1 (compact), else none. */
    mt_lines load_full = {0}, load_compact = {0};
    mt_load l;
    if (mt_snapshot_load(s, &l)) {
        mt_load_lines(f, &l, false, &load_full);
        mt_load_lines(f, &l, true, &load_compact);
    } else {
        mt_lines_push(f, &load_full, tui_line_span(f->a, mt_label("no load sample yet")));
        mt_lines_push(f, &load_compact, tui_line_span(f->a, mt_label("no load sample yet")));
    }
    uint16_t lf_h = (uint16_t)(load_full.n + 2), lc_h = (uint16_t)(load_compact.n + 2);
    uint16_t avail = tui_sat_sub(inner.height, 7);
    /* In order of preference: (net full?, spark rows, load full?/compact/none); the grid keeps 3 rows (4 for the roomiest). */
    static const struct {
        bool nf;
        uint16_t sp;
        uint8_t ld;
        uint16_t g;
    } picks[5] = {{true, 4, 2, 4}, {true, 2, 2, 3}, {true, 2, 1, 3}, {false, 2, 1, 3}, {false, 2, 0, 3}};
    uint16_t net_h, spark_h, load_h;
    const mt_lines *net_rows, *load_rows;
    mt_lines none = {0};
    size_t pk = 5;
    for (size_t i = 0; i < 5; i++) {
        uint16_t lh[3] = {0, lc_h, lf_h};
        if ((uint32_t)(picks[i].nf ? full_h : compact_h) + picks[i].sp + lh[picks[i].ld] + picks[i].g <= avail) {
            pk = i;
            break;
        }
    }
    if (pk < 5) {
        net_h = picks[pk].nf ? full_h : compact_h;
        net_rows = picks[pk].nf ? &net_full : &net_compact;
        spark_h = picks[pk].sp;
        load_h = picks[pk].ld == 2 ? lf_h : picks[pk].ld == 1 ? lc_h : 0;
        load_rows = picks[pk].ld == 2 ? &load_full : picks[pk].ld == 1 ? &load_compact : &none;
    } else {
        uint16_t h = tui_min16(compact_h, tui_sat_sub(avail, 2));
        net_h = h;
        spark_h = tui_min16(tui_sat_sub(avail, h), 2);
        net_rows = &net_compact;
        load_h = 0;
        load_rows = &none;
    }
    tui_rect r[6];
    tui_constraint cons[6] = {TUI_LENGTH(6), TUI_LENGTH(1), TUI_LENGTH(spark_h), TUI_LENGTH(net_h), TUI_LENGTH(load_h), TUI_FILL(1)};
    tui_vsplit(inner, 6, cons, r);
    tui_rect info = r[0], chips = r[1], spark = r[2], net = r[3], load = r[4], grid = r[5];

    uint32_t sysk = mt_snapshot_host_u32(s, MESH_S_SYSTEM);
    const char *sys = sysk == MESH_PEER_NONE ? "single" : MT_FMT(f, "system %u", sysk);
    bool green = mt_snapshot_host_u32(s, MESH_S_GREEN) == 1;
    size_t launched = 0;
    uint32_t halted = 0, failed = 0;
    for (size_t i = 0; i < s->nsections; i++) {
        const mt_section *p = &s->sections[i];
        if (mt_section_running(p)) launched += mt_section_nodes(p);
        halted += mt_section_p(p, MESH_P_HALTED);
        if (mt_section_state(p) == MESH_ST_FAILED) failed++;
    }
    bool cuda = mesh_backend_kind(mt_snapshot_head(s, MESH_H_BACKEND)) == MESH_BACKEND_CUDA;
    uint32_t iso = mt_snapshot_host_u32(s, MESH_S_ISOLATION);
    tui_line nodes_line = MT_LINE(f, mt_label("nodes "), mt_val(MT_FMT(f, "%u/%zu", mt_snapshot_nodes_online(s), launched)),
                                  mt_label(cuda ? " online  SMs " : " online  cores "),
                                  mt_val(MT_FMT(f, "%u/%u", mt_snapshot_host_u32(s, MESH_S_SMS), mt_snapshot_host_u32(s, MESH_S_SMS_TOTAL))),
                                  mt_label("  green ctx "), green ? mt_fgb("yes", MT_GOOD) : mt_fgs("no", MT_DIM), mt_label("  isolation "),
                                  mt_val(iso == MESH_ISOLATION_MPS ? "mps" : iso == MESH_ISOLATION_LEASE ? "lease" : "blink"));
    if (failed > 0)
        tui_line_push(f->a, &nodes_line, mt_fgb(MT_FMT(f, "  %u failed", failed), MT_BAD));
    else if (halted > 0 && launched == 0)
        tui_line_push(f->a, &nodes_line, mt_fgs("  idle", MT_DIM));
    float rtt = mt_snapshot_host_f32(s, MESH_S_PEER_RTT_US);
    char pdir[193];
    mt_snapshot_host_str(s, MESH_S_PROGRAMS_DIR, 64, pdir);
    tui_line status = MT_LINE(f, mt_label("programs dir "), mt_fgs(tui_arena_strdup(f->a, pdir), MT_DIM));
    tui_line inf;
    if (mt_infer_spans(f, s, &inf)) status = inf;
    if (!m->online || strcmp(m->status, "online") != 0) status = MT_LINE(f, mt_label("status "), mt_fgs(m->status, MT_BAD));
    char gpu[97], cc[32];
    mt_snapshot_gpu(s, gpu);
    mt_cc_label(mt_snapshot_head(s, MESH_H_CC), cc);
    const char *gl = cuda ? MT_FMT(f, " cc %s  ", cc)
                          : MT_FMT(f, " %s, %u threads/threadgroup, leases  ", mesh_host_label(f, mt_snapshot_head(s, MESH_H_BACKEND)),
                                   mt_snapshot_head(s, MESH_H_MAX_TG_THREADS));
    mt_jobs j = mt_snapshot_jobs(s);
    uint32_t me;
    bool has_me = mt_snapshot_system(s, &me);
    tui_line lines[6] = {
        MT_LINE(f, mt_label("GPU "), mt_val(tui_arena_strdup(f->a, gpu)), mt_label(gl), mt_fgs(sys, MT_ACCENT)),
        nodes_line,
        MT_LINE(f, mt_label("blinks "), mt_val(MT_FMT(f, "%u", mt_snapshot_host_u32(s, MESH_S_EPOCH))), mt_label("  last "),
                mt_val(MT_FMT(f, "%uus", mt_snapshot_host_u32(s, MESH_S_BLINK_US))), mt_label("  up "),
                mt_val(mt_fmt_uptime(f, mt_snapshot_host_u64(s, MESH_S_UPTIME_MS)))),
        MT_LINE(f, mt_label("tasks/s "), mt_fgb(mt_fmt_rate(f, mt_snapshot_host_f32(s, MESH_S_TASKS_PER_S)), MT_WARN), mt_label("  programs "),
                mt_val(MT_FMT(f, "%u", mt_snapshot_host_u32(s, MESH_S_PROGRAMS))), mt_label("  heap free "),
                mt_val(MT_FMT(f, "%zu KiB", (size_t)mt_snapshot_host_u32(s, MESH_S_HEAP_FREE) * MESH_VIEW_PAGE / 1024)), mt_label("  jobs "),
                mt_val(j.total == 0 ? "-" : MT_FMT(f, "%u active of %u", j.active, j.total))),
        MT_LINE(f, mt_label("peer "), mt_peer_span(f, s), mt_label(has_me ? "  linked " : ""),
                mt_val(has_me ? MT_FMT(f, "%u/%u", popcount(mt_snapshot_host_u32(s, MESH_S_LINKED)), mt_snapshot_systems(s)) : ""),
                mt_label("  RTT "), mt_val(rtt > 0.0f ? MT_FMT(f, "%sus", mt_ff(f, rtt, 1)) : "-")),
        status,
    };
    mt_par(f, tui_text_of(f->a, 6, lines), info);

    tui_line chip = tui_line_span(f->a, mt_label("programs "));
    for (size_t i = 0; i < s->nsections; i++) {
        const mt_section *p = &s->sections[i];
        const char *name = mt_sec_name(f, p);
        uint8_t c[3];
        mt_prog_rgb(name, mt_section_slot(p), c);
        tui_style st;
        switch (mt_section_state(p)) {
        case MESH_ST_RUNNING: st = mt_fg_bg_bold(MT_BG, mt_rgb(c, 1.0f)); break;
        case MESH_ST_FAILED: st = mt_fg_bg_bold(TUI_WHITE, MT_BAD); break;
        default: st = tui_fg_bg(mt_rgb(c, 0.8f), MT_FAINT); break;
        }
        tui_line_push(f->a, &chip, mt_styled(MT_FMT(f, "[%s]", name), st));
        tui_line_push(f->a, &chip, tui_span_raw(" "));
    }
    mt_par_line(f, chip, chips);

    size_t from = m->ntps > spark.width ? m->ntps - spark.width : 0;
    uint64_t peak = 0;
    for (size_t i = from; i < m->ntps; i++)
        if (m->tps[i] > peak) peak = m->tps[i];
    tui_block sb = tui_block_new();
    sb.borders = TUI_BORDER_TOP;
    sb.border_style = tui_fg(MT_FAINT);
    tui_block_add_title(f->a, &sb, MT_LINE(f, mt_label(" tasks/s "), mt_fgs(MT_FMT(f, "peak %llu ", (unsigned long long)peak), MT_DIM)),
                        TUI_TITLE_DEFAULT);
    tui_sparkline sp = {0};
    sp.block = &sb;
    sp.data = m->tps + from;
    sp.n = m->ntps - from;
    sp.style = tui_fg(MT_WARN);
    tui_sparkline_render(f->a, &sp, spark, f->buf);
    mt_draw_net_box(f, net, m, net_rows);
    mt_draw_load_box(f, load, s, load_rows);
    draw_sm_grid(f, grid, m, s, now);
}

void mt_draw_mesh(mt_frame *f, tui_rect area, const mt_app *app, mt_instant now) {
    mt_cluster_row *rows;
    size_t nrows = mt_cluster_rows(app, &rows);
    /* The overview: one row per system (and per single machine), above the panels. */
    uint16_t want = (uint16_t)(nrows + 3);
    uint16_t ov_h = nrows >= 2 ? tui_max16(tui_min16(want, (uint16_t)(area.height / 2)), tui_min16(4, area.height)) : 0;
    tui_rect r[2];
    tui_constraint cons[2] = {TUI_LENGTH(ov_h), TUI_FILL(1)};
    tui_vsplit(area, 2, cons, r);
    if (ov_h > 0) draw_overview(f, r[0], app, rows, nrows);
    free(rows);
    tui_rect panels = r[1];
    /* Every machine's panel side by side while each gets 40 columns; else the focused one. */
    size_t n = app->nmachines;
    if (n * 40 <= panels.width || n == 1) {
        tui_constraint *c = tui_arena_alloc(f->a, n * sizeof *c);
        tui_rect *cols = tui_arena_alloc(f->a, n * sizeof *cols);
        for (size_t i = 0; i < n; i++) c[i] = TUI_FILL(1);
        tui_hsplit(panels, n, c, cols);
        for (size_t i = 0; i < n; i++) draw_machine(f, cols[i], &app->machines[i], i == app->focus, now);
    } else {
        draw_machine(f, panels, mt_app_focused(app), true, now);
    }
}
