/* The tables gc_tables.c holds (generated). */
#ifndef TUI_GC_TABLES_H
#define TUI_GC_TABLES_H

#include <stdint.h>

typedef enum { TUI_GC_Any, TUI_GC_CR, TUI_GC_Control, TUI_GC_Extend, TUI_GC_Extended_Pictographic, TUI_GC_InCB_Consonant,
    TUI_GC_L, TUI_GC_LF, TUI_GC_LV, TUI_GC_LVT, TUI_GC_Prepend, TUI_GC_Regional_Indicator, TUI_GC_SpacingMark, TUI_GC_T,
    TUI_GC_V, TUI_GC_ZWJ } tui_gc_cat;

typedef struct { uint32_t lo, hi; tui_gc_cat cat; } tui_gc_range;

extern const tui_gc_range TUI_GC_TABLE[1618];
extern const uint32_t TUI_GC_INCB_EXTEND[377][2];

#endif
