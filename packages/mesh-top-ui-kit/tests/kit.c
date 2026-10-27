/* The kit's formats against Rust's (values from ui.rs's helpers run under cargo). */
#include "mesh/check.h"
#include "mesh/top_ui_kit.h"

#include <math.h>

int main(void) {
    tui_arena a = {0};
    mt_frame f = {&a, NULL};
    CHECK_S(mt_fmt_uptime(&f, 3723000), "1h02m03s");
    CHECK_S(mt_fmt_uptime(&f, 59999), "0m59s");
    CHECK_S(mt_fmt_rate(&f, 1234567.0f), "1.23M");
    CHECK_S(mt_fmt_rate(&f, 12345.0f), "12.3k");
    CHECK_S(mt_fmt_rate(&f, 0.25f), "0.2");
    CHECK_S(mt_fmt_us(&f, 1500.0f), "1.5ms");
    CHECK_S(mt_fmt_us(&f, 2.5f), "2us");
    CHECK_S(mt_fmt_us(&f, NAN), "0");
    CHECK_S(mt_fmt_bytes(&f, 0), "-");
    CHECK_S(mt_fmt_bytes(&f, 17u << 20), "17.0M");
    CHECK_S(mt_fmt_bytes(&f, (uint64_t)3 << 29), "1.5G");
    CHECK_S(mt_pct(&f, 0.355f), "36%");
    CHECK_S(mt_pct(&f, 20.0f), "999%");
    CHECK_S(mt_mask_list(&f, 0x15), "0,2,4");
    CHECK_S(mt_short_gpu(&f, "NVIDIA GeForce RTX 3060"), "RTX 3060");
    CHECK_S(mt_pad(&f, "ab", 5, '^'), " ab  ");
    CHECK_S(mt_take(&f, "→abc", 2), "→a");
    CHECK_U(mt_sat_u8(-3.0f), 0);
    CHECK_U(mt_sat_u8(300.0f), 255);
    CHECK_U(mt_sat_u8(NAN), 0);
    tui_color c = mt_rgb((const uint8_t[3]){200, 100, 50}, 1.5f);
    CHECK_U(c.r, 255);
    CHECK_U(c.g, 150);
    CHECK_U(c.b, 75);
    c = mt_lerp((const uint8_t[3]){70, 80, 120}, (const uint8_t[3]){16, 18, 30}, 0.5f);
    CHECK_U(c.r, 43);
    CHECK_U(c.g, 49);
    CHECK_U(c.b, 75);
    tui_arena_free(&a);
    return mesh_check_done();
}
