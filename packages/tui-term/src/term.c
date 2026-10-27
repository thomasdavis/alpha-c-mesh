/* termios raw mode and the xterm private modes, as crossterm sets and unsets them. */
#include "tui/term.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios saved;
static bool raw;
static volatile sig_atomic_t winch;

static void on_winch(int sig) {
    (void)sig;
    winch = 1;
}

bool tui_term_write(const char *buf, size_t len) {
    while (len) {
        ssize_t n = write(STDOUT_FILENO, buf, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        buf += n;
        len -= (size_t)n;
    }
    return true;
}

bool tui_term_enter(void) {
    if (raw) return true;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;
    if (tcgetattr(STDIN_FILENO, &saved) != 0) return false;
    struct termios t = saved;
    /* cfmakeraw (crossterm's enable_raw_mode). */
    t.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cflag &= ~(tcflag_t)(CSIZE | PARENB);
    t.c_cflag |= CS8;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) return false;
    raw = true;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);
    /* Alternate screen, hidden cursor, cleared. */
    static const char on[] = "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
    tui_term_write(on, sizeof on - 1);
    return true;
}

void tui_term_leave(void) {
    if (!raw) return;
    static const char off[] = "\x1b[0m\x1b[?25h\x1b[?1049l";
    tui_term_write(off, sizeof off - 1);
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    raw = false;
}

bool tui_term_size(uint16_t *width, uint16_t *height) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 && ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != 0) return false;
    *width = ws.ws_col;
    *height = ws.ws_row;
    return true;
}

bool tui_term_resized(void) {
    if (!winch) return false;
    winch = 0;
    return true;
}

long tui_term_read(char *buf, size_t cap, int timeout_ms) {
    struct pollfd p = {STDIN_FILENO, POLLIN, 0};
    int r = poll(&p, 1, timeout_ms);
    if (r < 0) return errno == EINTR ? 0 : -1;
    if (r == 0) return 0;
    ssize_t n = read(STDIN_FILENO, buf, cap);
    if (n < 0) return errno == EINTR || errno == EAGAIN ? 0 : -1;
    if (n == 0) return -1;
    return n;
}
