/* ratatui-widgets 0.3.2's Sparkline (every bar present: no absent values). */
#include "tui/sparkline.h"

static const char *const NINE_LEVELS[9] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

/* floor(a * b / c) for b < 2^20, in 128 bits made of two u64 (ratatui computes in u128). */
static uint64_t mul_div(uint64_t a, uint64_t b, uint64_t c) {
    uint64_t lo_part = (a & 0xFFFFFFFFu) * b, hi_part = (a >> 32) * b;
    uint64_t lo = lo_part + (hi_part << 32);
    uint64_t hi = (hi_part >> 32) + (lo < lo_part);
    uint64_t q = 0, r = 0;
    for (int i = 127; i >= 0; i--) {
        uint64_t bit = i >= 64 ? (hi >> (i - 64)) & 1 : (lo >> i) & 1;
        uint64_t top = r >> 63;
        r = r << 1 | bit;
        if (top || r >= c) {
            r -= c;
            if (i < 64) q |= (uint64_t)1 << i;
        }
    }
    return q; /* the quotient's high half is 0 for our inputs (value * ticks / max <= ticks when value <= max) */
}

static uint64_t scale_height(uint64_t value, uint64_t max, uint16_t max_height) {
    if (max == 0) return 0;
    uint64_t max_ticks = (uint64_t)max_height * 8;
    uint64_t ticks = mul_div(value, max_ticks, max);
    /* A quotient past 2^64 (value far above max) still clamps to max_ticks. */
    if (value > max && value / max >= max_ticks) return max_ticks;
    return ticks < max_ticks ? ticks : max_ticks;
}

void tui_sparkline_render(tui_arena *a, const tui_sparkline *s, tui_rect area, tui_buffer *buf) {
    if (s->block) tui_block_render(a, s->block, area, buf);
    tui_rect sa = tui_block_inner_opt(s->block, area);
    if (tui_rect_empty(sa)) return;
    uint64_t max = s->max;
    if (!s->has_max) {
        max = 1;
        if (s->n) {
            max = 0;
            for (size_t i = 0; i < s->n; i++)
                if (s->data[i] > max) max = s->data[i];
        }
    }
    size_t count = s->n < sa.width ? s->n : sa.width;
    for (size_t i = 0; i < count; i++) {
        uint16_t x = s->right_to_left ? (uint16_t)(tui_rect_right(sa) - i - 1) : (uint16_t)(sa.x + i);
        uint64_t h = scale_height(s->data[i], max, sa.height);
        for (uint16_t j = sa.height; j-- > 0;) {
            const char *sym = NINE_LEVELS[h > 8 ? 8 : h];
            h = h > 8 ? h - 8 : 0;
            tui_cell *c = tui_buffer_cell(buf, x, (uint16_t)(sa.y + j));
            if (!c) continue;
            tui_cell_set_str(c, sym);
            tui_cell_set_style(c, s->style);
        }
    }
}
