/* native.rs's pipe, spawn, describe and exit_word. */
#include "mesh/spawn.h"
#include "mesh/text.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static char *os_error(int e) { return mesh_fmt("%s (os error %d)", strerror(e), e); }

bool mesh_pipe_cloexec(int fds[2], char **err) {
    if (pipe(fds) != 0) {
        char *e = os_error(errno);
        mesh_errf(err, "native: pipe: %s", e);
        free(e);
        return false;
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
}

static bool overridden(char *const *env, const char *kv) {
    const char *eq = strchr(kv, '=');
    size_t kn = eq ? (size_t)(eq - kv) : strlen(kv);
    for (size_t i = 0; env && env[i]; i++)
        if (strncmp(env[i], kv, kn) == 0 && env[i][kn] == '=') return true;
    return false;
}

bool mesh_spawn(char *const *argv, char *const *env, const mesh_spawn_fd *fds, size_t nfds, pid_t *pid, char **err) {
    size_t n = 0, m = 0;
    while (environ[n]) n++;
    while (env && env[m]) m++;
    char **envp = mesh_alloc((n + m + 1) * sizeof *envp);
    size_t k = 0;
    for (size_t i = 0; i < n; i++)
        if (!overridden(env, environ[i])) envp[k++] = environ[i];
    for (size_t i = 0; i < m; i++) envp[k++] = env[i];
    envp[k] = NULL;
    /* Every source above the targets first, so no dup2 overwrites a later source. */
    int *high = mesh_alloc((nfds ? nfds : 1) * sizeof *high);
    bool ok = true;
    for (size_t i = 0; i < nfds && ok; i++) {
        high[i] = fcntl(fds[i].parent, F_DUPFD_CLOEXEC, 10);
        if (high[i] < 0) {
            char *e = os_error(errno);
            mesh_errf(err, "dup: %s", e);
            free(e);
            for (size_t j = 0; j < i; j++) close(high[j]);
            ok = false;
        }
    }
    if (ok) {
        posix_spawn_file_actions_t fa;
        posix_spawnattr_t at;
        posix_spawn_file_actions_init(&fa);
        posix_spawnattr_init(&at);
        for (size_t i = 0; i < nfds; i++) posix_spawn_file_actions_adddup2(&fa, high[i], fds[i].child);
        posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&at, 0);
        int r = posix_spawnp(pid, argv[0], &fa, &at, argv, envp);
        posix_spawn_file_actions_destroy(&fa);
        posix_spawnattr_destroy(&at);
        for (size_t i = 0; i < nfds; i++) close(high[i]);
        if (r != 0) {
            char *e = os_error(r);
            mesh_errf(err, "posix_spawn %s: %s", argv[0], e);
            free(e);
            ok = false;
        }
    }
    free(high);
    free(envp);
    return ok;
}

char *mesh_describe_status(int st) {
    if (WIFEXITED(st)) return mesh_fmt("exit %d", WEXITSTATUS(st));
    if (WIFSIGNALED(st)) {
        int sig = WTERMSIG(st);
        return mesh_fmt("signal %d (%s)%s", sig, strsignal(sig), WCOREDUMP(st) ? ", core dumped" : "");
    }
    return mesh_fmt("wait status %d", st);
}

uint32_t mesh_exit_word(int st) {
    uint32_t code = WIFEXITED(st) ? (uint32_t)WEXITSTATUS(st) : 0, sig = WIFSIGNALED(st) ? (uint32_t)WTERMSIG(st) : 0;
    return (code & 0xffffffu) << 8 | (sig & 0xffu);
}

bool mesh_status_success(int st) { return WIFEXITED(st) && WEXITSTATUS(st) == 0; }
