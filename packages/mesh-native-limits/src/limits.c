/* native.rs's Limits, limits_available and check_cgroup. */
#include "mesh/native_limits.h"
#include "mesh/log.h"
#include "mesh/text.h"

#include "mesh/abi/control_block.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

mesh_native_limits mesh_native_limits_of(const mesh_manifest *m) {
    return (mesh_native_limits){m->cpu_pct, m->mem, m->nice, m->background};
}

bool mesh_native_limits_scoped(const mesh_native_limits *l) { return l->cpu_pct > 0 || l->mem > 0; }

bool mesh_native_limits_eq(const mesh_native_limits *a, const mesh_native_limits *b) {
    return a->cpu_pct == b->cpu_pct && a->mem == b->mem && a->nice == b->nice && a->background == b->background;
}

uint32_t mesh_native_limits_bits(const mesh_native_limits *l) {
    return (l->cpu_pct > 0 ? MESH_LIMIT_CPU : 0u) | (l->mem > 0 ? MESH_LIMIT_MEM : 0u) | (l->nice > 0 ? MESH_LIMIT_NICE : 0u) |
           (l->background ? MESH_LIMIT_BG : 0u);
}

char *mesh_native_limits_debug(const mesh_native_limits *l) {
    return mesh_fmt("Limits { cpu_pct: %u, mem: %llu, nice: %u, background: %s }", (unsigned)l->cpu_pct, (unsigned long long)l->mem,
                    (unsigned)l->nice, l->background ? "true" : "false");
}

static pthread_mutex_t probe_mu = PTHREAD_MUTEX_INITIALIZER;
static bool probed;
static char *probe_err; /* NULL: works */

/* Runs argv, capturing stderr; its wait status, or -1 when it could not start (errno). */
static int run_capture(char *const argv[], mesh_text *err_out) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        int devnull = open("/dev/null", 0);
        if (devnull >= 0) dup2(devnull, 0);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    char buf[512];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof buf)) > 0) mesh_text_putn(err_out, buf, (size_t)n);
    close(fds[0]);
    int st;
    waitpid(pid, &st, 0);
    return st;
}

bool mesh_native_limits_available(const mesh_native_limits *l, char **err) {
    if (!mesh_native_limits_scoped(l)) return true;
#if defined(__APPLE__)
    const char *what = l->cpu_pct > 0 && l->mem > 0 ? "CPU or memory" : l->cpu_pct > 0 ? "CPU" : "memory";
    return mesh_errf(err,
                     "load: limits: macOS cannot cap a process's %s (no cgroups; setrlimit RLIMIT_AS/DATA/RSS is refused or not enforced): "
                     "remove `cpu`/`mem_mb` from the manifest (`nice`, `background` work)",
                     what);
#else
    pthread_mutex_lock(&probe_mu);
    if (!probed) {
        probed = true;
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        char *argv[] = {"systemd-run", "--user", "--scope", "-q", "-p", "CPUQuota=100%", "-p", "MemoryMax=64M", "-p", "MemorySwapMax=0", "--", "true", NULL};
        mesh_text e = {0};
        int st = run_capture(argv, &e);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        if (st >= 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0) {
            mesh_info("native: limits: systemd-run --user --scope works (%.0f ms)",
                      (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6);
        } else if (st >= 0 && !(WIFEXITED(st) && WEXITSTATUS(st) == 127)) {
            const char *s = mesh_text_str(&e);
            size_t n = e.len;
            mesh_trim(&s, &n);
            char *d = WIFEXITED(st) ? mesh_fmt("exit status: %d", WEXITSTATUS(st)) : mesh_fmt("signal: %d", WTERMSIG(st));
            probe_err = mesh_fmt("load: limits: `systemd-run --user --scope` failed (%s): %.*s", d, (int)n, s);
            free(d);
        } else {
            probe_err = mesh_fmt("load: limits: cannot run systemd-run: No such file or directory (os error 2)");
        }
        mesh_text_free(&e);
    }
    bool ok = probe_err == NULL;
    if (!ok) mesh_errf(err, "%s", probe_err);
    pthread_mutex_unlock(&probe_mu);
    return ok;
#endif
}

static char *read_trimmed(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char buf[512];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    const char *s = buf;
    mesh_trim(&s, &n);
    return mesh_strndup(s, n);
}

bool mesh_native_check_cgroup(pid_t pid, const mesh_native_limits *l, char **err) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/cgroup", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) {
        int e = errno;
        return mesh_errf(err, "limits: /proc/%d/cgroup: %s (os error %d)", (int)pid, strerror(e), e);
    }
    char line[1024], *cg = NULL;
    while (!cg && fgets(line, sizeof line, f))
        if (strncmp(line, "0::", 3) == 0) {
            const char *s = line + 3;
            size_t n = strlen(s);
            mesh_trim(&s, &n);
            cg = mesh_strndup(s, n);
        }
    fclose(f);
    if (!cg) return mesh_errf(err, "limits: no cgroup v2 entry");
    size_t cn = strlen(cg);
    if (cn < 6 || strcmp(cg + cn - 6, ".scope") != 0) {
        mesh_errf(err, "limits: worker %d runs in %s, not a systemd scope", (int)pid, cg);
        free(cg);
        return false;
    }
    bool ok = true;
    if (l->cpu_pct > 0) {
        char *p = mesh_fmt("/sys/fs/cgroup%s/cpu.max", cg), *v = read_trimmed(p);
        free(p);
        unsigned long long q = 0, per = 0;
        bool good = v && sscanf(v, "%llu %llu", &q, &per) == 2 && per > 0 && q * 100 / per == l->cpu_pct;
        if (!v) ok = mesh_errf(err, "limits: %s/cpu.max: cannot read", cg);
        else if (!good) {
            char *q2 = mesh_debug_str(v);
            ok = mesh_errf(err, "limits: %s: cpu.max is %s, wanted %u%% of a core", cg, q2, (unsigned)l->cpu_pct);
            free(q2);
        }
        free(v);
    }
    if (ok && l->mem > 0) {
        char *p1 = mesh_fmt("/sys/fs/cgroup%s/memory.max", cg), *p2 = mesh_fmt("/sys/fs/cgroup%s/memory.swap.max", cg);
        char *m = read_trimmed(p1), *s = read_trimmed(p2);
        free(p1);
        free(p2);
        char want[32];
        snprintf(want, sizeof want, "%llu", (unsigned long long)l->mem);
        if (!m || !s) ok = mesh_errf(err, "limits: %s/memory.max: cannot read", cg);
        else if (strcmp(m, want) != 0 || strcmp(s, "0") != 0)
            ok = mesh_errf(err, "limits: %s: memory.max %s, memory.swap.max %s; wanted %llu and 0", cg, m, s, (unsigned long long)l->mem);
        free(m);
        free(s);
    }
    free(cg);
    return ok;
}
