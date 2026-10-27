/* 1900 random widget configurations (paragraphs, tables, charts, sparklines, blocks, lines),
 * built from the same xorshift64 sequence as the Rust that rendered tests/data/widgets.txt with
 * ratatui 0.30.2: every cell's symbol must match, and an FNV-1a hash of every cell's colours and
 * modifiers. The Rust is in docs/port/top.md; every RNG draw here mirrors one there, in order. */
#include "mesh/check.h"
#include "tui/chart.h"
#include "tui/paragraph.h"
#include "tui/sparkline.h"
#include "tui/table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng = 0x2545f4914f6cdd1du;
static uint64_t next(void) {
    uint64_t x = rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng = x;
    return x;
}
static uint64_t below(uint64_t n) { return n == 0 ? 0 : next() % n; }
static bool chance(uint64_t k) { return below(k) == 0; }

static const char *WORDS[16] = {"the", "quick", "brown", "fox", "日本", "→", "  ", "  indent", "supercalifragilistic",
                                "a", "b", "\t", "µs", "x y z", "████", "é"};

static tui_arena A;

static tui_color color(void) {
    switch (below(4)) {
    case 0: return TUI_RESET;
    case 1: return TUI_WHITE;
    case 2: return TUI_BLACK;
    default: {
        uint8_t r = (uint8_t)below(256), g = (uint8_t)below(256), b = (uint8_t)below(256);
        return TUI_RGB(r, g, b);
    }
    }
}
static tui_style style(void) {
    tui_style s = tui_style_new();
    if (chance(2)) s = tui_style_fg(s, color());
    if (chance(3)) s = tui_style_bg(s, color());
    if (chance(3)) s = tui_style_add(s, TUI_BOLD);
    if (chance(5)) s = tui_style_remove(s, TUI_BOLD);
    if (chance(6)) s = tui_style_add(s, TUI_REVERSED);
    return s;
}
static int8_t align(void) {
    static const int8_t a[3] = {TUI_ALIGN_LEFT, TUI_ALIGN_CENTER, TUI_ALIGN_RIGHT};
    return a[below(3)];
}
static const char *words(uint64_t max) {
    size_t n = (size_t)below(max);
    char buf[512] = "";
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && chance(2)) strcat(buf, " ");
        strcat(buf, WORDS[below(16)]);
    }
    return tui_arena_strdup(&A, buf);
}
static tui_line line(void) {
    size_t n = (size_t)below(4);
    tui_line l = tui_line_empty();
    for (size_t i = 0; i < n; i++) {
        const char *w = words(4);
        tui_line_push(&A, &l, tui_span_styled(w, style()));
    }
    l.style = style();
    if (chance(3)) l.align = align();
    return l;
}
static tui_text text(void) {
    size_t n = (size_t)below(6);
    tui_text t = tui_text_empty();
    for (size_t i = 0; i < n; i++) tui_text_push(&A, &t, line());
    if (chance(3)) t.style = style();
    return t;
}
static const tui_border_set *SETS[4] = {&TUI_BORDER_PLAIN, &TUI_BORDER_ROUNDED, &TUI_BORDER_DOUBLE, &TUI_BORDER_THICK};
static tui_block *block(void) {
    tui_block *b = tui_arena_alloc(&A, sizeof *b);
    *b = tui_block_new();
    b->borders = (uint8_t)below(16);
    b->set = SETS[below(4)];
    if (chance(2)) b->border_style = style();
    if (chance(3)) b->style = style();
    uint64_t nt = below(4);
    for (uint64_t i = 0; i < nt; i++) {
        tui_line t = line();
        if (chance(2)) t.align = align();
        tui_block_add_title(&A, b, t, chance(3) ? TUI_TITLE_BOTTOM : TUI_TITLE_DEFAULT);
    }
    return b;
}
static tui_rect area(uint64_t w, uint64_t h) {
    uint16_t x = (uint16_t)below(3), y = (uint16_t)below(3), ww = (uint16_t)below(w), hh = (uint16_t)below(h);
    return tui_rect_new(x, y, ww, hh);
}

/* ---- the golden file ---- */

static char *gold;
static size_t gold_at;
static int bad;

static void cb(uint64_t *h, tui_color c) {
    uint8_t b[4] = {0, 0, 0, 0};
    if (c.kind == TUI_COLOR_RGB) {
        b[0] = 1;
        b[1] = c.r;
        b[2] = c.g;
        b[3] = c.b;
    } else if (c.kind == TUI_COLOR_NAMED) {
        b[0] = 3;
        b[1] = c.r;
        if (c.r != 0 && c.r != 15) b[0] = b[1] = b[2] = b[3] = 9;
    }
    for (int i = 0; i < 4; i++) {
        *h ^= b[i];
        *h *= 0x100000001b3u;
    }
}

static void check(const char *name, const tui_buffer *buf) {
    char *got = malloc(64 * 1024);
    size_t n = (size_t)snprintf(got, 64, "=== %s", name);
    uint64_t h = 0xcbf29ce484222325u;
    for (uint16_t y = 0; y < buf->area.height; y++) {
        got[n++] = '\n';
        for (uint16_t x = 0; x < buf->area.width; x++) {
            const tui_cell *c = tui_buffer_cell_c(buf, x, y);
            const char *s = tui_cell_symbol(c);
            size_t k = strlen(s);
            memcpy(got + n, s, k);
            n += k;
            cb(&h, c->fg);
            cb(&h, c->bg);
            uint8_t m[2] = {(uint8_t)(c->mod & 0xff), (uint8_t)(c->mod >> 8)};
            for (int i = 0; i < 2; i++) {
                h ^= m[i];
                h *= 0x100000001b3u;
            }
        }
    }
    n += (size_t)snprintf(got + n, 64, "\n--- %016llx\n", (unsigned long long)h);
    got[n] = 0;
    bool ok = strncmp(gold + gold_at, got, n) == 0;
    if (!ok && bad++ < 6) {
        size_t end = gold_at;
        while (gold[end] && strncmp(gold + end, "\n--- ", 5) != 0) end++;
        while (gold[end] && gold[end] != '\n') end++;
        while (gold[end] == '\n') end++;
        while (gold[end] && gold[end] != '\n') end++;
        fprintf(stderr, "want:\n%.*s\ngot:\n%s", (int)(end - gold_at), gold + gold_at, got);
    }
    CHECK(ok);
    /* Next case: after this case's "--- hash\n". */
    char *p = strstr(gold + gold_at, "\n--- ");
    if (p) {
        p = strchr(p + 1, '\n');
        gold_at = p ? (size_t)(p + 1 - gold) : strlen(gold);
    }
    free(got);
}

int main(void) {
    FILE *f = fopen("tests/data/widgets.txt", "rb");
    CHECK(f != NULL);
    if (!f) return mesh_check_done();
    gold = malloc(2u << 20);
    size_t glen = fread(gold, 1, (2u << 20) - 1, f);
    gold[glen] = 0;
    fclose(f);
    char name[64];

    for (int c = 0; c < 500; c++) {
        tui_buffer buf;
        tui_buffer_init(&buf, (tui_rect){0, 0, 32, 10});
        tui_paragraph p = tui_paragraph_new(text());
        switch (below(3)) {
        case 0: break;
        case 1: p.wrap = TUI_WRAP_TRIM; break;
        default: p.wrap = TUI_WRAP_KEEP; break;
        }
        p.alignment = align();
        if (chance(3)) p.scroll_y = (uint16_t)below(4);
        if (chance(3)) p.style = style();
        if (chance(2)) p.block = block();
        tui_rect a = area(30, 9);
        tui_paragraph_render(&A, &p, a, &buf);
        snprintf(name, sizeof name, "paragraph %d", c);
        check(name, &buf);
        tui_buffer_free(&buf);
        tui_arena_reset(&A);
    }
    for (int c = 0; c < 300; c++) {
        tui_buffer buf;
        tui_buffer_init(&buf, (tui_rect){0, 0, 40, 10});
        size_t ncols = 1 + (size_t)below(5), nrows = (size_t)below(9);
        tui_table_row *rows = tui_arena_alloc(&A, (nrows ? nrows : 1) * sizeof *rows);
        for (size_t i = 0; i < nrows; i++) {
            size_t n = (size_t)below(ncols + 1);
            rows[i] = tui_table_row_new();
            for (size_t k = 0; k < n; k++) {
                tui_table_cell cell;
                switch (below(3)) {
                case 0: cell = tui_table_cell_text(tui_text_from_str(&A, words(3))); break;
                case 1: {
                    const char *w = words(3);
                    cell = tui_table_cell_text(tui_text_line(&A, tui_line_span(&A, tui_span_styled(w, style()))));
                    break;
                }
                default: cell = tui_table_cell_text(tui_text_line(&A, line())); break;
                }
                if (chance(4)) cell.style = style();
                tui_table_row_push(&A, &rows[i], cell);
            }
            if (chance(4)) rows[i].style = style();
        }
        tui_constraint *w = tui_arena_alloc(&A, ncols * sizeof *w);
        for (size_t i = 0; i < ncols; i++) {
            switch (below(5)) {
            case 0: w[i] = TUI_LENGTH(below(12)); break;
            case 1: w[i] = TUI_MIN(below(8)); break;
            case 2: w[i] = TUI_MAX(below(10)); break;
            case 3: w[i] = TUI_PERCENTAGE(below(60)); break;
            default: w[i] = TUI_FILL(below(3)); break;
            }
        }
        tui_table t = tui_table_new(rows, nrows, w, ncols);
        t.column_spacing = (uint16_t)below(3);
        tui_table_row head;
        if (chance(2)) {
            head = tui_table_row_new();
            for (size_t k = 0; k < ncols; k++) {
                tui_table_cell cell;
                switch (below(3)) {
                case 0: cell = tui_table_cell_text(tui_text_from_str(&A, words(3))); break;
                case 1: {
                    const char *ws = words(3);
                    cell = tui_table_cell_text(tui_text_line(&A, tui_line_span(&A, tui_span_styled(ws, style()))));
                    break;
                }
                default: cell = tui_table_cell_text(tui_text_line(&A, line())); break;
                }
                if (chance(4)) cell.style = style();
                tui_table_row_push(&A, &head, cell);
            }
            head.style = style();
            t.header = &head;
        }
        if (chance(2)) {
            t.highlight_symbol = tui_text_from_str(&A, chance(2) ? "▶" : ">>");
            t.row_highlight_style = style();
        }
        if (chance(3)) t.block = block();
        tui_table_state st = {0};
        if (chance(2) && nrows > 0) {
            st.has_selected = true;
            st.selected = (size_t)below(nrows + 2);
        }
        tui_rect a = area(38, 9);
        tui_table_render(&A, &t, a, &buf, &st);
        snprintf(name, sizeof name, "table %d", c);
        check(name, &buf);
        tui_buffer_free(&buf);
        tui_arena_reset(&A);
    }
    const tui_marker markers[5] = {TUI_MARKER_DOT, TUI_MARKER_BRAILLE, TUI_MARKER_BLOCK, TUI_MARKER_HALF_BLOCK, TUI_MARKER_BAR};
    const tui_graph_type types[3] = {TUI_GRAPH_SCATTER, TUI_GRAPH_LINE, TUI_GRAPH_BAR};
    const tui_legend_position positions[8] = {TUI_LEGEND_TOP, TUI_LEGEND_TOP_RIGHT, TUI_LEGEND_TOP_LEFT, TUI_LEGEND_LEFT,
                                              TUI_LEGEND_RIGHT, TUI_LEGEND_BOTTOM, TUI_LEGEND_BOTTOM_RIGHT, TUI_LEGEND_BOTTOM_LEFT};
    for (int c = 0; c < 300; c++) {
        tui_buffer buf;
        tui_buffer_init(&buf, (tui_rect){0, 0, 44, 16});
        size_t nd = 1 + (size_t)below(3);
        tui_point *data[3];
        size_t counts[3];
        for (size_t d = 0; d < nd; d++) {
            counts[d] = (size_t)below(25);
            data[d] = tui_arena_alloc(&A, (counts[d] ? counts[d] : 1) * sizeof *data[d]);
            for (size_t k = 0; k < counts[d]; k++) {
                double x = (double)below(1400) / 100.0 - 2.0;
                double y = (double)below(1400) / 100.0 - 2.0;
                data[d][k] = (tui_point){x, y};
            }
        }
        tui_dataset sets[3];
        for (size_t d = 0; d < nd; d++) {
            sets[d] = tui_dataset_new();
            sets[d].data = data[d];
            sets[d].n = counts[d];
            sets[d].marker = markers[below(5)];
            sets[d].graph_type = types[below(3)];
            sets[d].style = style();
            if (chance(3)) {
                sets[d].has_name = true;
                sets[d].name = line();
            } else if (chance(2)) {
                sets[d].has_name = true;
                sets[d].name = tui_line_from_str(&A, words(3));
            }
        }
        tui_line *xl, *yl;
        size_t nxl, nyl;
        tui_chart ch = tui_chart_new(sets, nd);
        nxl = (size_t)below(4);
        xl = tui_arena_alloc(&A, (nxl ? nxl : 1) * sizeof *xl);
        for (size_t i = 0; i < nxl; i++) xl[i] = tui_line_from_str(&A, words(2));
        ch.x_axis.bounds[0] = 0.0;
        ch.x_axis.bounds[1] = 10.0;
        ch.x_axis.labels = xl;
        ch.x_axis.nlabels = nxl;
        ch.x_axis.style = style();
        ch.x_axis.labels_alignment = align();
        nyl = (size_t)below(4);
        yl = tui_arena_alloc(&A, (nyl ? nyl : 1) * sizeof *yl);
        for (size_t i = 0; i < nyl; i++) yl[i] = tui_line_from_str(&A, words(2));
        ch.y_axis.bounds[0] = -1.0;
        ch.y_axis.bounds[1] = 11.0;
        ch.y_axis.labels = yl;
        ch.y_axis.nlabels = nyl;
        ch.y_axis.style = style();
        ch.y_axis.labels_alignment = align();
        if (chance(2)) {
            ch.x_axis.has_title = true;
            ch.x_axis.title = line();
        }
        if (chance(2)) {
            ch.y_axis.has_title = true;
            ch.y_axis.title = line();
        }
        ch.style = style();
        if (chance(5)) ch.has_legend = false;
        else ch.legend_position = positions[below(8)];
        if (chance(3)) ch.block = block();
        tui_rect a = area(42, 14);
        tui_chart_render(&A, &ch, a, &buf);
        snprintf(name, sizeof name, "chart %d", c);
        check(name, &buf);
        tui_buffer_free(&buf);
        tui_arena_reset(&A);
    }
    for (int c = 0; c < 200; c++) {
        tui_buffer buf;
        tui_buffer_init(&buf, (tui_rect){0, 0, 30, 8});
        size_t n = (size_t)below(40);
        uint64_t *data = tui_arena_alloc(&A, (n ? n : 1) * sizeof *data);
        for (size_t i = 0; i < n; i++) data[i] = chance(8) ? UINT64_MAX - below(9) : below(200);
        tui_sparkline s = {.data = data, .n = n};
        s.style = style();
        if (chance(2)) {
            s.has_max = true;
            s.max = below(300);
        }
        if (chance(3)) s.right_to_left = true;
        if (chance(3)) s.block = block();
        tui_rect a = area(28, 7);
        tui_sparkline_render(&A, &s, a, &buf);
        snprintf(name, sizeof name, "sparkline %d", c);
        check(name, &buf);
        tui_buffer_free(&buf);
        tui_arena_reset(&A);
    }
    for (int c = 0; c < 300; c++) {
        tui_buffer buf;
        tui_buffer_init(&buf, (tui_rect){0, 0, 24, 6});
        tui_block *b = block();
        tui_rect a = area(22, 5);
        tui_block_render(&A, b, a, &buf);
        snprintf(name, sizeof name, "block %d", c);
        check(name, &buf);
        tui_buffer_free(&buf);
        tui_arena_reset(&A);
    }
    for (int c = 0; c < 300; c++) {
        tui_buffer buf;
        tui_buffer_init(&buf, (tui_rect){0, 0, 24, 3});
        tui_line l = line();
        tui_rect a = area(22, 2);
        tui_line l2 = l;
        if (chance(2)) l2.align = align();
        tui_line_render(&A, &l2, a, &buf, TUI_ALIGN_NONE);
        uint16_t x = (uint16_t)below(5), w = (uint16_t)below(24);
        tui_buffer_set_line(&buf, x, 2, &l, w);
        snprintf(name, sizeof name, "line %d", c);
        check(name, &buf);
        tui_buffer_free(&buf);
        tui_arena_reset(&A);
    }
    if (bad) fprintf(stderr, "%d of 1900 cases differ\n", bad);
    free(gold);
    tui_arena_free(&A);
    tui_layout_cache_clear();
    return mesh_check_done();
}
