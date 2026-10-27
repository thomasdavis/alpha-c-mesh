/* Spawning: descriptors land where asked, the environment is extended and
 * overridden, a process group of its own, statuses described. */
#include "mesh/check.h"
#include "mesh/spawn.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    int out[2];
    char *err = NULL;
    CHECK(mesh_pipe_cloexec(out, &err));
    setenv("MESH_SPAWN_A", "old", 1);
    char *argv[] = {"sh", "-c", "echo \"$MESH_SPAWN_A $MESH_SPAWN_B $(ps -o pgid= -p $$ | tr -d ' ') $$\" >&5", NULL};
    char *env[] = {"MESH_SPAWN_A=new", "MESH_SPAWN_B=b", NULL};
    mesh_spawn_fd fds[] = {{out[1], 5}};
    pid_t pid;
    CHECK(mesh_spawn(argv, env, fds, 1, &pid, &err));
    close(out[1]);
    char buf[128] = {0};
    ssize_t n = read(out[0], buf, sizeof buf - 1);
    int st;
    waitpid(pid, &st, 0);
    CHECK(n > 0);
    char a[16], b[16];
    long pgid, self;
    CHECK(sscanf(buf, "%15s %15s %ld %ld", a, b, &pgid, &self) == 4);
    CHECK_S(a, "new");
    CHECK_S(b, "b");
    CHECK_I(pgid, self);
    CHECK(mesh_status_success(st));
    char *d = mesh_describe_status(st);
    CHECK_S(d, "exit 0");
    free(d);
    /* Exit codes and signals. */
    char *e3[] = {"sh", "-c", "exit 3", NULL};
    CHECK(mesh_spawn(e3, NULL, NULL, 0, &pid, &err));
    waitpid(pid, &st, 0);
    d = mesh_describe_status(st);
    CHECK_S(d, "exit 3");
    free(d);
    CHECK_U(mesh_exit_word(st), 3 << 8);
    char *k9[] = {"sh", "-c", "kill -9 $$", NULL};
    CHECK(mesh_spawn(k9, NULL, NULL, 0, &pid, &err));
    waitpid(pid, &st, 0);
    d = mesh_describe_status(st);
    CHECK_S(d, "signal 9 (Killed)");
    free(d);
    CHECK_U(mesh_exit_word(st), 9);
    char *nope[] = {"/nonexistent/x", NULL};
    CHECK(!mesh_spawn(nope, NULL, NULL, 0, &pid, &err));
    CHECK_S(err, "posix_spawn /nonexistent/x: No such file or directory (os error 2)");
    free(err);
    return mesh_check_done();
}
