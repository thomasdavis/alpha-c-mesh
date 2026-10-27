/* The log: lines reach the fd with stamps and levels, the recent ring keeps the last ones,
 * a full queue drops and counts instead of blocking. */
#include "mesh/check.h"
#include "mesh/log.h"
#include "mesh/text.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    int fds[2];
    CHECK(pipe(fds) == 0);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    mesh_log_set_fd(fds[1]);
    mesh_info("hello %d", 1);
    mesh_error("bad %s", "thing");
    mesh_log_flush(2000);
    char buf[256] = {0};
    ssize_t n = read(fds[0], buf, sizeof buf - 1);
    CHECK(n > 0);
    /* "HH:MM:SS.mmm INFO  hello 1\nHH:MM:SS.mmm ERROR bad thing\n" */
    CHECK(strstr(buf, " INFO  hello 1\n") == buf + 12);
    CHECK(strstr(buf, " ERROR bad thing\n") != NULL);
    CHECK(buf[2] == ':' && buf[5] == ':' && buf[8] == '.');
    char **lines;
    size_t k = mesh_log_recent(5, &lines);
    CHECK_U(k, 2);
    CHECK(strstr(lines[1], "ERROR bad thing") != NULL);
    for (size_t i = 0; i < k; i++) free(lines[i]);
    free(lines);
    CHECK_U(mesh_log_dropped(), 0);
    /* Nobody reads: the pipe fills, then the queue; emitting still returns at once. */
    for (int i = 0; i < 20000; i++) mesh_info("line %d of a reader-less pipe, long enough to fill it quickly .......", i);
    CHECK(mesh_log_dropped() > 0);
    k = mesh_log_recent(2000, &lines);
    CHECK_U(k, 1000);
    CHECK(strstr(lines[999], "line 19999 ") != NULL);
    for (size_t i = 0; i < k; i++) free(lines[i]);
    free(lines);
    return mesh_check_done();
}
