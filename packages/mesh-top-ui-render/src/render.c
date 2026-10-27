/* ui_render.rs, ported. */
#include "mesh/top_ui_render.h"

#include "pathtrace/view.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float fractf(float x) { return x - truncf(x); }

tui_color mt_hue(float h) {
    float h0 = fractf(h) * 6.0f, s = 0.85f, v = 0.95f;
    uint32_t i = mt_sat_u32(floorf(h0));
    float fr = fractf(h0);
    float p = v * (1.0f - s), q = v * (1.0f - s * fr), t = v * (1.0f - s * (1.0f - fr));
    float r, g, b;
    switch (i % 6) {
    case 0: r = v, g = t, b = p; break;
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    default: r = v, g = p, b = q; break;
    }
    return TUI_RGB(mt_sat_u8(r * 255.0f), mt_sat_u8(g * 255.0f), mt_sat_u8(b * 255.0f));
}

/* Who rendered a tile: a fleet member (its system's colour), else its executor, coloured by node. */
static const char *owner(mt_frame *f, uint32_t system, uint32_t exec, tui_color *c) {
    if (system != MESH_PEER_NONE) {
        *c = mt_sys_color(system);
        return MT_FMT(f, "system %u", system);
    }
    uint32_t e = exec & 0xff;
    *c = mt_hue((float)((exec >> 8) & 0xff) * 0.618034f);
    return e == PT_EXEC_CUDA ? "cuda" : e == PT_EXEC_METAL ? "metal" : e == PT_EXEC_WASM ? "wasm" : "?";
}

static void rgb_of(tui_color c, float out[3]) {
    if (c.kind == TUI_COLOR_RGB) {
        out[0] = c.r;
        out[1] = c.g;
        out[2] = c.b;
    } else {
        out[0] = out[1] = out[2] = 128.0f;
    }
}

typedef struct {
    uint32_t passes;
    tui_color color;
} tile_meta;

typedef struct {
    uint32_t iw, ih, tx;
    float s;
    const uint8_t *px;
    const tile_meta *meta;
    bool marks;
} image;

static tui_color sample(const image *im, uint16_t ox, uint16_t oy) {
    uint32_t iw = im->iw, ih = im->ih;
    uint32_t sx0 = mt_sat_u32((float)ox / im->s), sx1 = mt_sat_u32((float)(uint16_t)(ox + 1) / im->s);
    uint32_t sy0 = mt_sat_u32((float)oy / im->s), sy1 = mt_sat_u32((float)(uint16_t)(oy + 1) / im->s);
    if (sx1 > iw) sx1 = iw;
    if (sy1 > ih) sy1 = ih;
    sx1 = sx1 > sx0 + 1 ? sx1 : sx0 + 1;
    if (sx1 > iw) sx1 = iw;
    sy1 = sy1 > sy0 + 1 ? sy1 : sy0 + 1;
    if (sy1 > ih) sy1 = ih;
    uint32_t r = 0, g = 0, b = 0, n = 0;
    for (uint32_t y = sy0; y < sy1; y++)
        for (uint32_t x = sx0; x < sx1; x++) {
            uint32_t t = (y / PT_TILE_H) * im->tx + x / PT_TILE_W;
            size_t o = 4 * ((size_t)t * PT_TILE_PIX + (y % PT_TILE_H) * PT_TILE_W + x % PT_TILE_W);
            r += im->px[o];
            g += im->px[o + 1];
            b += im->px[o + 2];
            n++;
        }
    if (n < 1) n = 1;
    float rf = (float)(r / n), gf = (float)(g / n), bf = (float)(b / n);
    if (im->marks) {
        uint32_t cx = (sx0 + sx1) / 2, cy = (sy0 + sy1) / 2;
        if (cx > iw - 1) cx = iw - 1;
        if (cy > ih - 1) cy = ih - 1;
        const tile_meta *mt = &im->meta[(cy / PT_TILE_H) * im->tx + cx / PT_TILE_W];
        float m[3] = {40.0f, 40.0f, 48.0f};
        if (mt->passes != 0) rgb_of(mt->color, m);
        /* Tile edges in the owner's colour, the inside half tinted. */
        bool edge = sx0 % PT_TILE_W < (sx1 - sx0) || sy0 % PT_TILE_H < (sy1 - sy0);
        float a = edge ? 0.8f : 0.35f;
        rf = rf * (1.0f - a) + m[0] * a;
        gf = gf * (1.0f - a) + m[1] * a;
        bf = bf * (1.0f - a) + m[2] * a;
    }
    return TUI_RGB(mt_sat_u8(rf), mt_sat_u8(gf), mt_sat_u8(bf));
}

/* The image (tile-major 0xffBBGGRR bytes, iw x ih) fitted into img_a, box-filtered. */
static void draw_image(mt_frame *f, tui_rect img_a, uint32_t iw, uint32_t ih, const uint8_t *px, const tile_meta *meta, bool marks) {
    image im = {iw, ih, iw / PT_TILE_W, 0.0f, px, meta, marks};
    float sa = (float)img_a.width / (float)iw, sb = 2.0f * (float)img_a.height / (float)ih;
    im.s = sa <= sb ? sa : sb; /* f32::min */
    if (sa != sa) im.s = sb;
    uint16_t ow = tui_max16(mt_sat_u16((float)iw * im.s), 1), oh = tui_max16(mt_sat_u16((float)ih * im.s), 2);
    uint16_t x0 = (uint16_t)(img_a.x + (img_a.width - tui_min16(ow, img_a.width)) / 2);
    uint16_t y0 = (uint16_t)(img_a.y + (img_a.height - tui_min16((uint16_t)(oh / 2), img_a.height)) / 2);
    for (uint16_t cy = 0; cy < tui_min16((uint16_t)(oh / 2), img_a.height); cy++)
        for (uint16_t cx = 0; cx < tui_min16(ow, img_a.width); cx++) {
            tui_color top = sample(&im, cx, (uint16_t)(2 * cy)), bot = sample(&im, cx, (uint16_t)(2 * cy + 1));
            tui_cell *cell = tui_buffer_cell(f->buf, (uint16_t)(x0 + cx), (uint16_t)(y0 + cy));
            if (!cell) continue;
            tui_cell_set_str(cell, "▀");
            cell->fg = top;
            cell->bg = bot;
        }
}

static const char *repeat(mt_frame *f, const char *s, size_t n) {
    size_t l = strlen(s);
    char *o = tui_arena_alloc(f->a, l * n + 1);
    for (size_t i = 0; i < n; i++) memcpy(o + i * l, s, l);
    return o;
}

/* Fleet mode: a job's image as the focused member assembles it, tiles by machine. */
static void draw_fleet(mt_frame *f, tui_rect area, const mt_app *app, const mt_fleet_image *img) {
    const mt_machine *m = mt_app_focused(app);
#define G(k) mt_fleet_image_get(img, k)
#define N(k) mt_fleet_image_num(img, k)
    uint64_t items = N("items"), folded = N("folded"), tiles = img->ntiles;
    double pct = items > 0 ? 100.0 * (double)folded / (double)items : 0.0;
    const char *sha = G("sha256");
    const char *extra = MT_FMT(f, "%s job %s  %sx%s  %s/%llu tiles  passes %s%%  %llu spp  %s Msamples/s  %s  assembled on system %s%s", G("program"),
                               G("job"), G("width"), G("height"), G("tiles_done"), (unsigned long long)tiles, mt_ff(f, pct, 1),
                               (unsigned long long)(N("passes") * N("spp")), mt_ff(f, img->msps, 2), G("tone"), G("system"),
                               strlen(sha) == 64 ? MT_FMT(f, "  sha256 %.16s..", sha) : "");
    tui_color color;
    tui_line title = mt_machine_title(f, m, true, extra, &color);
    tui_block block = mt_panel(f, title, color);
    tui_rect inner = tui_block_inner(&block, area);
    mt_block(f, &block, area);
    tui_rect r[3];
    tui_vsplit(inner, 3, (tui_constraint[]){TUI_FILL(1), TUI_LENGTH(1), TUI_LENGTH(1)}, r);
    tile_meta *meta = tui_arena_alloc(f->a, (tiles ? tiles : 1) * sizeof *meta);
    for (size_t t = 0; t < tiles; t++) {
        meta[t].passes = img->tiles[t][0];
        meta[t].color = img->tiles[t][1] == UINT32_MAX ? MT_DIM : mt_sys_color(img->tiles[t][1]);
    }
    uint32_t iw = (uint32_t)N("width"), ih = (uint32_t)N("height");
    if (iw >= PT_TILE_W && ih >= PT_TILE_H) draw_image(f, r[0], iw, ih, img->px, meta, app->render_marks);
    /* Progress: the passes folded (each machine's share in its colour), the state, the digests. */
    uint64_t w = tui_min16(tui_sat_sub(r[1].width, 2), 60);
    tui_line spans = tui_line_span(f->a, tui_span_raw(" "));
    size_t used = 0;
    uint64_t cum = 0;
    for (size_t i = 0; i < img->nmembers; i++) {
        /* Cumulative rounding: the shares fill exactly the folded part. */
        cum += img->members[i].folded;
        size_t c = items ? (size_t)(cum * w / items) : 0;
        size_t cells = c > used ? c - used : 0;
        used += cells;
        tui_line_push(f->a, &spans, mt_fgs(repeat(f, "█", cells), mt_sys_color(img->members[i].k)));
    }
    tui_line_push(f->a, &spans, mt_fgs(repeat(f, "░", w > used ? w - used : 0), MT_DIM));
    tui_line_push(f->a, &spans, tui_span_raw(MT_FMT(f, " %llu/%llu passes  %s  acc %s  pixels %s", (unsigned long long)folded,
                                                    (unsigned long long)items, G("state"), G("acc"), G("pixels"))));
    if (N("bad") > 0) tui_line_push(f->a, &spans, mt_fgs(MT_FMT(f, "  %s bad payloads", G("bad")), MT_BAD));
    if (N("unavailable") > 0) tui_line_push(f->a, &spans, mt_fgs(MT_FMT(f, "  %s blobs held by no member up", G("unavailable")), MT_BAD));
    mt_par_line(f, spans, r[1]);
    /* The legend: per machine, its passes folded (share) and the tiles whose last pass is its. */
    tui_line legend = tui_line_span(f->a, mt_label(app->render_marks ? " tiles by machine: " : " machines: "));
    for (size_t i = 0; i < img->nmembers; i++) {
        uint32_t k = img->members[i].k;
        const char *host = "";
        for (size_t q = 0; q < app->nmachines; q++) {
            const mt_machine *x = &app->machines[q];
            uint32_t sk;
            if (x->has_snap && mt_snapshot_system(&x->snap, &sk) && sk == k) {
                char h[97];
                mt_snapshot_hostname(&x->snap, h);
                host = tui_arena_strdup(f->a, h);
                break;
            }
        }
        uint64_t n = img->members[i].folded;
        tui_line_push(f->a, &legend, mt_fgs("■ ", mt_sys_color(k)));
        tui_line_push(f->a, &legend, tui_span_raw(MT_FMT(f, "%u%s %llu (%s%%) %u tiles  ", k, *host ? MT_FMT(f, " %s", host) : "",
                                                         (unsigned long long)n, mt_ff(f, folded > 0 ? 100.0 * (double)n / (double)folded : 0.0, 0),
                                                         img->members[i].tiles)));
    }
    mt_par_line(f, legend, r[2]);
#undef G
#undef N
}

static void center_line(mt_frame *f, tui_rect area, tui_span s) {
    tui_paragraph p = tui_paragraph_new(tui_text_line(f->a, tui_line_span(f->a, s)));
    p.alignment = TUI_ALIGN_CENTER;
    tui_paragraph_render(f->a, &p, mt_rect(area.x, area.y, area.width, 1), f->buf);
}

typedef struct {
    const char *name;
    tui_color c;
    uint32_t n;
    uint32_t nodes[256];
    size_t nnodes;
} owner_count;

void mt_draw_render(mt_frame *f, tui_rect area, const mt_app *app) {
    const mt_machine *m = mt_app_focused(app);
    if (m->has_fleet) {
        draw_fleet(f, area, app, &m->fleet);
        return;
    }
    char pname[64];
    uint32_t tiles = 0;
    bool has_prog = mt_app_render_program(app, pname, &tiles);
    const mt_section *p = has_prog ? mt_machine_program(m, pname) : NULL;
#define W(o) mt_section_view_u32(p, (o))
    const char *title_extra = "render";
    if (has_prog && p) {
        uint32_t vt = W(PT_V_TILES), passes = W(PT_V_PASSES), spp = W(PT_V_SPP);
        uint64_t done_passes = 0;
        for (uint32_t t = 0; t < vt; t++) done_passes += W(pt_view_meta(t) + PT_M_PASSES) & ~PT_FOLDING;
        uint64_t total = (uint64_t)vt * passes;
        float items_s = 0.0f;
        for (size_t i = 0; i < MESH_COUNTERS; i++) {
            char cn[3 * MESH_NAME_BYTES + 1];
            mt_section_counter_name(p, i, cn);
            if (strcmp(cn, "items") == 0) {
                items_s = mt_machine_counter_rate(m, mt_section_slot(p), i);
                break;
            }
        }
        double msps = (double)items_s * (double)PT_TILE_PIX * (double)spp / 1e6;
        title_extra = MT_FMT(f, "%s  %ux%u  %u/%u tiles  passes %s%%  %llu spp  %s Msamples/s  %s", pname, W(PT_V_WIDTH), W(PT_V_HEIGHT), W(PT_V_DONE),
                             vt, mt_ff(f, total > 0 ? 100.0 * (double)done_passes / (double)total : 0.0, 1),
                             (unsigned long long)(done_passes * spp / (vt > 1 ? vt : 1)), mt_ff(f, msps, 2), W(PT_V_TONEMAP) == 1 ? "reinhard" : "aces");
    }
    tui_color color;
    tui_line title = mt_machine_title(f, m, true, title_extra, &color);
    tui_block block = mt_panel(f, title, color);
    tui_rect inner = tui_block_inner(&block, area);
    mt_block(f, &block, area);
    if (!has_prog || !p) {
        center_line(f, inner,
                    mt_styled(" pathtrace is not loaded here: `load pathtrace-live` + `start pathtrace-live`, or `load pathtrace` + `job submit "
                              "pathtrace items 9600` ",
                              tui_fg_bg(MT_BG, MT_WARN)));
        return;
    }
    tui_rect r[2];
    tui_vsplit(inner, 2, (tui_constraint[]){TUI_FILL(1), TUI_LENGTH(1)}, r);
    uint32_t iw = W(PT_V_WIDTH), ih = W(PT_V_HEIGHT);
    /* Every tile's owner and passes, from the snapshot's meta. */
    tile_meta *meta = tui_arena_alloc(f->a, (tiles ? tiles : 1) * sizeof *meta);
    const char **names = tui_arena_alloc(f->a, (tiles ? tiles : 1) * sizeof *names);
    for (uint32_t t = 0; t < tiles; t++) {
        uint32_t b = pt_view_meta(t);
        meta[t].passes = W(b + PT_M_PASSES) & ~PT_FOLDING;
        names[t] = owner(f, W(b + PT_M_SYSTEM), W(b + PT_M_EXEC), &meta[t].color);
    }
    bool have_px = m->has_render && m->render_len == (size_t)tiles * PT_TILE_PIX * 4;
    if (have_px) {
        draw_image(f, r[0], iw, ih, m->render_px, meta, app->render_marks);
    } else {
        const char *msg = m->render_err ? m->render_err : "fetching the image";
        center_line(f, r[0], mt_fgs(MT_FMT(f, " %s ", msg), MT_DIM));
    }
    /* The legend: tiles per owner (rendered at least one pass). */
    owner_count *owners = tui_arena_alloc(f->a, (tiles ? tiles : 1) * sizeof *owners);
    size_t no = 0;
    for (uint32_t t = 0; t < tiles; t++) {
        if (meta[t].passes == 0) continue;
        uint32_t node = (W(pt_view_meta(t) + PT_M_EXEC) >> 8) & 0xff;
        owner_count *o = NULL;
        for (size_t i = 0; i < no && !o; i++)
            if (strcmp(owners[i].name, names[t]) == 0) o = &owners[i];
        if (!o) {
            o = &owners[no++];
            o->name = names[t];
            o->c = meta[t].color;
        }
        o->n++;
        bool seen = false;
        for (size_t i = 0; i < o->nnodes; i++) seen |= o->nodes[i] == node;
        if (!seen && o->nnodes < 256) o->nodes[o->nnodes++] = node;
    }
    tui_line spans = tui_line_span(f->a, mt_label(app->render_marks ? " tiles by renderer: " : " tiles: "));
    for (size_t i = 0; i < no; i++) {
        tui_line_push(f->a, &spans, mt_fgs("■ ", owners[i].c));
        const char *by = strncmp(owners[i].name, "system", 6) == 0 ? "" : MT_FMT(f, " (last pass on %zu nodes)", owners[i].nnodes);
        tui_line_push(f->a, &spans, tui_span_raw(MT_FMT(f, "%s %u%s  ", owners[i].name, owners[i].n, by)));
    }
    tui_line_push(f->a, &spans, mt_label(MT_FMT(f, " gen %u items %u", W(PT_V_GEN), W(PT_V_ITEMS))));
    mt_par_line(f, spans, r[1]);
#undef W
}
