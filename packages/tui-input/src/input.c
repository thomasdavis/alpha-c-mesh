/* Key decoding after crossterm 0.29's unix parser (parse_event): the subset of sequences xterm,
 * VTE, tmux and the Linux console send for these keys. */
#include "tui/input.h"

#include "tui/utf8.h"

static tui_key mk(tui_key_code c, uint8_t mods) { return (tui_key){c, 0, 0, mods, TUI_KEY_PRESS}; }
static tui_key fkey(uint8_t f, uint8_t mods) { return (tui_key){TUI_KEY_F, 0, f, mods, TUI_KEY_PRESS}; }

/* xterm's modifier parameter: 1 + (shift 1 | alt 2 | ctrl 4). */
static uint8_t mods_of(unsigned p) {
    if (p < 2) return 0;
    p--;
    return (uint8_t)(((p & 1) ? TUI_MOD_SHIFT : 0) | ((p & 2) ? TUI_MOD_ALT : 0) | ((p & 4) ? TUI_MOD_CONTROL : 0));
}

static size_t csi(const char *in, size_t len, tui_key *k) {
    /* in[0] == ESC, in[1] == '['. Parameters, then a final byte 0x40..0x7E. */
    unsigned params[4] = {0, 0, 0, 0};
    int np = 0;
    size_t i = 2;
    if (i < len && (in[i] == '<' || in[i] == '?' || in[i] == '>')) i++; /* private markers: consumed, ignored */
    for (; i < len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c >= '0' && c <= '9') {
            if (np == 0) np = 1;
            if (np <= 4) params[np - 1] = params[np - 1] * 10 + (c - '0');
        } else if (c == ';' || c == ':') {
            if (np == 0) np = 1;
            np++;
        } else if (c >= 0x40 && c <= 0x7E) {
            uint8_t m = np >= 2 ? mods_of(params[1]) : 0;
            *k = mk(TUI_KEY_NONE, 0);
            switch (c) {
            case 'A': *k = mk(TUI_KEY_UP, m); break;
            case 'B': *k = mk(TUI_KEY_DOWN, m); break;
            case 'C': *k = mk(TUI_KEY_RIGHT, m); break;
            case 'D': *k = mk(TUI_KEY_LEFT, m); break;
            case 'H': *k = mk(TUI_KEY_HOME, m); break;
            case 'F': *k = mk(TUI_KEY_END, m); break;
            case 'Z': *k = mk(TUI_KEY_BACKTAB, TUI_MOD_SHIFT); break;
            case 'P': *k = fkey(1, m); break;
            case 'Q': *k = fkey(2, m); break;
            case 'R': *k = fkey(3, m); break;
            case 'S': *k = fkey(4, m); break;
            case '~':
                switch (params[0]) {
                case 1: case 7: *k = mk(TUI_KEY_HOME, m); break;
                case 2: *k = mk(TUI_KEY_INSERT, m); break;
                case 3: *k = mk(TUI_KEY_DELETE, m); break;
                case 4: case 8: *k = mk(TUI_KEY_END, m); break;
                case 5: *k = mk(TUI_KEY_PAGE_UP, m); break;
                case 6: *k = mk(TUI_KEY_PAGE_DOWN, m); break;
                case 11: case 12: case 13: case 14: case 15: *k = fkey((uint8_t)(params[0] == 15 ? 5 : params[0] - 10), m); break;
                case 17: case 18: case 19: case 20: case 21: *k = fkey((uint8_t)(params[0] - 11), m); break;
                case 23: case 24: *k = fkey((uint8_t)(params[0] - 12), m); break;
                default: break;
                }
                break;
            default: break; /* mouse, focus, cursor reports: consumed */
            }
            return i + 1;
        } else if (c < 0x20 || c > 0x7E) {
            *k = mk(TUI_KEY_NONE, 0);
            return i; /* malformed: drop what came before */
        }
    }
    return 0; /* incomplete */
}

size_t tui_input_parse(const char *in, size_t len, bool quiet, tui_key *k) {
    *k = mk(TUI_KEY_NONE, 0);
    if (len == 0) return 0;
    unsigned char b = (unsigned char)in[0];
    if (b == 0x1B) {
        if (len == 1) {
            if (!quiet) return 0;
            *k = mk(TUI_KEY_ESC, 0);
            return 1;
        }
        if (in[1] == '[') return csi(in, len, k);
        if (in[1] == 'O') {
            if (len < 3) return quiet ? (*k = mk(TUI_KEY_ESC, 0), 1) : 0;
            switch (in[2]) {
            case 'A': *k = mk(TUI_KEY_UP, 0); break;
            case 'B': *k = mk(TUI_KEY_DOWN, 0); break;
            case 'C': *k = mk(TUI_KEY_RIGHT, 0); break;
            case 'D': *k = mk(TUI_KEY_LEFT, 0); break;
            case 'H': *k = mk(TUI_KEY_HOME, 0); break;
            case 'F': *k = mk(TUI_KEY_END, 0); break;
            case 'P': *k = fkey(1, 0); break;
            case 'Q': *k = fkey(2, 0); break;
            case 'R': *k = fkey(3, 0); break;
            case 'S': *k = fkey(4, 0); break;
            default: break;
            }
            return 3;
        }
        if (in[1] == 0x1B) {
            *k = mk(TUI_KEY_ESC, 0);
            return 1;
        }
        /* Alt + key. */
        tui_key inner;
        size_t n = tui_input_parse(in + 1, len - 1, quiet, &inner);
        if (n == 0) return 0;
        inner.mods |= TUI_MOD_ALT;
        *k = inner;
        return n + 1;
    }
    if (b == '\r' || b == '\n') {
        *k = mk(TUI_KEY_ENTER, 0);
        return 1;
    }
    if (b == '\t') {
        *k = mk(TUI_KEY_TAB, 0);
        return 1;
    }
    if (b == 0x7F || b == 0x08) {
        *k = mk(TUI_KEY_BACKSPACE, 0);
        return 1;
    }
    if (b == 0) {
        *k = (tui_key){TUI_KEY_CHAR, ' ', 0, TUI_MOD_CONTROL, TUI_KEY_PRESS};
        return 1;
    }
    if (b >= 0x01 && b <= 0x1A) {
        *k = (tui_key){TUI_KEY_CHAR, (uint32_t)('a' + b - 1), 0, TUI_MOD_CONTROL, TUI_KEY_PRESS};
        return 1;
    }
    if (b >= 0x1C && b <= 0x1F) {
        *k = (tui_key){TUI_KEY_CHAR, (uint32_t)('4' + b - 0x1C), 0, TUI_MOD_CONTROL, TUI_KEY_PRESS};
        return 1;
    }
    /* A character: wait for the rest of a UTF-8 sequence. */
    size_t need = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
    if (len < need) return quiet ? (*k = mk(TUI_KEY_NONE, 0), len) : 0;
    size_t i = 0;
    uint32_t cp = tui_utf8_next(in, need, &i);
    *k = tui_key_char(cp);
    if (cp < 0x80 && cp >= 'A' && cp <= 'Z') k->mods = TUI_MOD_SHIFT;
    return i;
}
