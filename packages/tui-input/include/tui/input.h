/* @ajax/tui-input: crossterm's key decoding for the keys a TUI uses: printable characters (UTF-8),
 * Ctrl+letter, Enter, Tab, Shift-Tab, Backspace, Esc, the arrows, Home/End/Insert/Delete,
 * PageUp/PageDown and F1-F12, from both CSI and SS3 sequences. A lone ESC is ambiguous until no
 * more bytes come: the caller says when input has gone quiet. */
#ifndef TUI_INPUT_H
#define TUI_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TUI_KEY_NONE,
    TUI_KEY_CHAR,
    TUI_KEY_ENTER,
    TUI_KEY_ESC,
    TUI_KEY_BACKSPACE,
    TUI_KEY_TAB,
    TUI_KEY_BACKTAB,
    TUI_KEY_UP,
    TUI_KEY_DOWN,
    TUI_KEY_LEFT,
    TUI_KEY_RIGHT,
    TUI_KEY_HOME,
    TUI_KEY_END,
    TUI_KEY_PAGE_UP,
    TUI_KEY_PAGE_DOWN,
    TUI_KEY_INSERT,
    TUI_KEY_DELETE,
    TUI_KEY_F,
} tui_key_code;

enum { TUI_MOD_SHIFT = 1, TUI_MOD_CONTROL = 2, TUI_MOD_ALT = 4 };
enum { TUI_KEY_PRESS, TUI_KEY_REPEAT, TUI_KEY_RELEASE };

typedef struct {
    tui_key_code code;
    uint32_t ch;  /* TUI_KEY_CHAR: the character */
    uint8_t f;    /* TUI_KEY_F: 1..12 */
    uint8_t mods; /* TUI_MOD_* */
    uint8_t kind; /* TUI_KEY_PRESS */
} tui_key;

static inline tui_key tui_key_char(uint32_t c) { return (tui_key){TUI_KEY_CHAR, c, 0, 0, TUI_KEY_PRESS}; }
static inline tui_key tui_key_of(tui_key_code code) { return (tui_key){code, 0, 0, 0, TUI_KEY_PRESS}; }

/* Decodes one key from in[0..len). Returns the bytes it used: 0 when more bytes are needed
 * (a partial sequence, or a lone ESC while quiet is false), and *key is TUI_KEY_NONE when the
 * bytes were consumed but meant nothing to us (unknown sequences, mouse reports). */
size_t tui_input_parse(const char *in, size_t len, bool quiet, tui_key *key);

#endif
