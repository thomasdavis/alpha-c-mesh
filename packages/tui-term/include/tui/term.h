/* @ajax/tui-term: the terminal as ratatui::init/restore (crossterm) leave it: raw mode
 * (cfmakeraw's flags), the alternate screen, the cursor hidden; restore undoes each, and is safe
 * to call twice or without enter. Reads wait up to a timeout with poll(2); SIGWINCH sets a flag. */
#ifndef TUI_TERM_H
#define TUI_TERM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Raw mode, alternate screen, hidden cursor, a clear screen; false (and nothing changed) when
 * stdin or stdout isn't a terminal. */
bool tui_term_enter(void);
void tui_term_leave(void);
/* The window's size in cells (TIOCGWINSZ on stdout, then stdin). */
bool tui_term_size(uint16_t *width, uint16_t *height);
/* Whether a SIGWINCH arrived since the last call (installed by tui_term_enter). */
bool tui_term_resized(void);
/* Reads what is available from stdin within timeout_ms (-1: wait, 0: don't). Returns the bytes
 * read, 0 on timeout, -1 on error or end of input. */
long tui_term_read(char *buf, size_t cap, int timeout_ms);
/* Writes all of buf to stdout (retrying short writes). */
bool tui_term_write(const char *buf, size_t len);

#endif
