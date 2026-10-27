/* Without a terminal (as under the test runner): enter refuses and changes nothing, leave is
 * harmless, reads from a pipe time out and then deliver. */
#include "mesh/check.h"
#include "tui/term.h"

#include <unistd.h>

int main(void) {
    int fds[2];
    CHECK(pipe(fds) == 0);
    int saved = dup(STDIN_FILENO);
    dup2(fds[0], STDIN_FILENO);
    CHECK(!tui_term_enter());
    tui_term_leave();
    char buf[16];
    CHECK_I(tui_term_read(buf, sizeof buf, 10), 0);
    CHECK(write(fds[1], "ab", 2) == 2);
    CHECK_I(tui_term_read(buf, sizeof buf, 100), 2);
    CHECK(buf[0] == 'a' && buf[1] == 'b');
    CHECK(!tui_term_resized());
    dup2(saved, STDIN_FILENO);
    close(fds[0]);
    close(fds[1]);
    close(saved);
    return mesh_check_done();
}
