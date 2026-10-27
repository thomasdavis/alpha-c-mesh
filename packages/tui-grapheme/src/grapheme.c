/* UAX #29 extended grapheme clusters, forwards: the pair rules plus the three that need context
 * (GB9c conjuncts, GB11 emoji ZWJ sequences, GB12/13 regional indicator pairs), carried as small
 * state machines while walking the cluster. */
#include "tui/grapheme.h"

#include "gc_tables.h"
#include "tui/utf8.h"

#include <stdbool.h>
#include <stdint.h>

static tui_gc_cat category(uint32_t c) {
    size_t lo = 0, hi = sizeof TUI_GC_TABLE / sizeof TUI_GC_TABLE[0];
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < TUI_GC_TABLE[mid].lo) hi = mid;
        else if (c > TUI_GC_TABLE[mid].hi) lo = mid + 1;
        else return TUI_GC_TABLE[mid].cat;
    }
    return TUI_GC_Any;
}

static bool incb_extend(uint32_t c) {
    size_t lo = 0, hi = sizeof TUI_GC_INCB_EXTEND / sizeof TUI_GC_INCB_EXTEND[0];
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < TUI_GC_INCB_EXTEND[mid][0]) hi = mid;
        else if (c > TUI_GC_INCB_EXTEND[mid][1]) lo = mid + 1;
        else return true;
    }
    return false;
}

static bool incb_linker(uint32_t c) {
    switch (c) {
    case 0x94D: case 0x9CD: case 0xACD: case 0xB4D: case 0xC4D: case 0xD4D: case 0x1039: case 0x17D2:
    case 0x1A60: case 0x1B44: case 0x1BAB: case 0xA9C0: case 0xAAF6: case 0x10A3F: case 0x11133:
    case 0x113D0: case 0x1193E: case 0x11A47: case 0x11A99: case 0x11F42:
        return true;
    default:
        return false;
    }
}

/* The context carried along the cluster. */
typedef struct {
    int emoji;   /* 0 none, 1 ExtPict Extend*, 2 ExtPict Extend* ZWJ */
    int incb;    /* 0 none, 1 Consonant (Extend|Linker)* without a linker, 2 with one */
    int ri;      /* regional indicators in a row */
} context;

static void advance(context *x, uint32_t c, tui_gc_cat cat) {
    if (cat == TUI_GC_Extended_Pictographic) x->emoji = 1;
    else if (cat == TUI_GC_Extend) x->emoji = x->emoji == 1 ? 1 : 0;
    else if (cat == TUI_GC_ZWJ) x->emoji = x->emoji == 1 ? 2 : 0;
    else x->emoji = 0;

    if (cat == TUI_GC_InCB_Consonant) x->incb = 1;
    else if (incb_linker(c)) x->incb = x->incb ? 2 : 0;
    else if (incb_extend(c)) x->incb = x->incb;
    else x->incb = 0;

    x->ri = cat == TUI_GC_Regional_Indicator ? x->ri + 1 : 0;
}

static bool is_break(tui_gc_cat a, tui_gc_cat b, const context *x) {
    if (a == TUI_GC_CR && b == TUI_GC_LF) return false;                                        /* GB3 */
    if (a == TUI_GC_Control || a == TUI_GC_CR || a == TUI_GC_LF) return true;                  /* GB4 */
    if (b == TUI_GC_Control || b == TUI_GC_CR || b == TUI_GC_LF) return true;                  /* GB5 */
    if (a == TUI_GC_L && (b == TUI_GC_L || b == TUI_GC_V || b == TUI_GC_LV || b == TUI_GC_LVT)) /* GB6 */
        return false;
    if ((a == TUI_GC_LV || a == TUI_GC_V) && (b == TUI_GC_V || b == TUI_GC_T)) return false;  /* GB7 */
    if ((a == TUI_GC_LVT || a == TUI_GC_T) && b == TUI_GC_T) return false;                    /* GB8 */
    if (b == TUI_GC_Extend || b == TUI_GC_ZWJ) return false;                                  /* GB9 */
    if (b == TUI_GC_SpacingMark) return false;                                                /* GB9a */
    if (a == TUI_GC_Prepend) return false;                                                    /* GB9b */
    if (b == TUI_GC_InCB_Consonant && x->incb == 2) return false;                             /* GB9c */
    if (a == TUI_GC_ZWJ && b == TUI_GC_Extended_Pictographic && x->emoji == 2) return false;   /* GB11 */
    if (a == TUI_GC_Regional_Indicator && b == TUI_GC_Regional_Indicator && x->ri % 2 == 1)    /* GB12/13 */
        return false;
    return true; /* GB999 */
}

size_t tui_grapheme_next(const char *s, size_t len, size_t start) {
    if (start >= len) return len;
    size_t i = start;
    uint32_t c = tui_utf8_next(s, len, &i);
    tui_gc_cat a = category(c);
    context x = {0, 0, 0};
    advance(&x, c, a);
    while (i < len) {
        size_t at = i;
        uint32_t d = tui_utf8_next(s, len, &i);
        tui_gc_cat b = category(d);
        if (is_break(a, b, &x)) return at;
        advance(&x, d, b);
        a = b;
    }
    return len;
}
