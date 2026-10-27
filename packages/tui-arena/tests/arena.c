/* The arena: alignment, zeroing, growth in place and by copy, big blocks, reset and free. */
#include "mesh/check.h"
#include "tui/arena.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    tui_arena a = {0};
    char *s = tui_arena_fmt(&a, "%s-%d", "frame", 42);
    CHECK_S(s, "frame-42");
    for (int i = 0; i < 1000; i++) {
        unsigned char *p = tui_arena_alloc(&a, (size_t)i + 1);
        CHECK(((uintptr_t)p & 15) == 0);
        int zero = 1;
        for (int j = 0; j <= i; j++) zero &= p[j] == 0;
        CHECK(zero);
        memset(p, 0xab, (size_t)i + 1);
    }
    int *v = tui_arena_alloc(&a, 4 * sizeof *v);
    for (int i = 0; i < 4; i++) v[i] = i;
    int *w = tui_arena_grow(&a, v, 4 * sizeof *v, 64 * sizeof *v);
    CHECK(w == v); /* the last allocation grows in place */
    CHECK_I(w[3], 3);
    CHECK_I(w[63], 0);
    (void)tui_arena_alloc(&a, 8);
    int *x = tui_arena_grow(&a, w, 64 * sizeof *w, 128 * sizeof *w);
    CHECK(x != w); /* not the last any more: copied */
    CHECK_I(x[3], 3);
    char *big = tui_arena_alloc(&a, 1 << 20);
    CHECK(big[0] == 0 && big[(1 << 20) - 1] == 0);
    CHECK_S(tui_arena_strndup(&a, "abcdef", 3), "abc");
    tui_arena_reset(&a);
    CHECK_S(tui_arena_strdup(&a, "again"), "again");
    tui_arena_free(&a);
    tui_arena_free(&a);
    return mesh_check_done();
}
