/* Rust's Debug quoting of a string (core::fmt, char::escape_debug), for ASCII and UTF-8 text. */
#include "mesh/debug_str.h"

#include <string.h>

void mdebug_str(msb *b, const char *s, size_t n) {
    msb_putc(b, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': msb_puts(b, "\\\""); break;
        case '\\': msb_puts(b, "\\\\"); break;
        case '\t': msb_puts(b, "\\t"); break;
        case '\r': msb_puts(b, "\\r"); break;
        case '\n': msb_puts(b, "\\n"); break;
        case 0: msb_puts(b, "\\0"); break;
        default:
            if (c < 0x20 || c == 0x7f) msb_printf(b, "\\u{%x}", c);
            else msb_putc(b, (char)c);
        }
    }
    msb_putc(b, '"');
}

char *mdebug(const char *s) {
    msb b = {0};
    mdebug_str(&b, s, strlen(s));
    return msb_take(&b);
}
