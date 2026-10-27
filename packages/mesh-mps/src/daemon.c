/* mps.rs's daemon half: our private nvidia-cuda-mps-control, its requests,
 * stale daemons of dead meshds, and the exit cleanup of workers and daemon. */
#include "mps_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

char *mps_os_error(int e) { return mesh_fmt("%s (os error %d)", strerror(e), e); }

uint64_t mps_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

void mps_sleep_us(unsigned us) {
    struct timespec t = {(time_t)(us / 1000000), (long)(us % 1000000) * 1000L};
    nanosleep(&t, NULL);
}

/* Rust's ExitStatus Display: "exit status: 1", "signal: 9 (SIGKILL)". */
char *mps_status_display(int st) {
    if (WIFEXITED(st)) return mesh_fmt("exit status: %d", WEXITSTATUS(st));
    if (WIFSIGNALED(st)) return mesh_fmt("signal: %d (SIG%s)", WTERMSIG(st), sigabbrev_np(WTERMSIG(st)) ? sigabbrev_np(WTERMSIG(st)) : "?");
    return mesh_fmt("wait status: %d", st);
}

static bool mkdir_private(const char *p, char **err) {
    /* create_dir_all */
    char *tmp = mesh_strdup(p);
    for (char *s = tmp + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            mkdir(tmp, 0755);
            *s = '/';
        }
    if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
        char *e = mps_os_error(errno);
        mesh_errf(err, "mps: %s: %s", p, e);
        free(e);
        free(tmp);
        return false;
    }
    free(tmp);
    if (chmod(p, 0700) != 0) {
        char *e = mps_os_error(errno);
        mesh_errf(err, "mps: chmod %s: %s", p, e);
        free(e);
        return false;
    }
    return true;
}

static void read_all(int fd, mesh_text *t) {
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) mesh_text_putn(t, buf, (size_t)n);
}

/* The env of a request to our daemon. */
static char **daemon_env(const char *dir) {
    char **env = mesh_alloc(3 * sizeof *env);
    env[0] = mesh_fmt("%s=%s/pipe", MESH_MPS_PIPE_ENV, dir);
    env[1] = mesh_fmt("%s=%s/log", MESH_MPS_LOG_ENV, dir);
    return env;
}

static void free_env(char **env) {
    for (size_t i = 0; env[i]; i++) free(env[i]);
    free(env);
}

char *mps_control(const char *dir, const char *req, char **err) {
    int in[2], out[2], er[2];
    if (!mesh_pipe_cloexec(in, err) || !mesh_pipe_cloexec(out, err) || !mesh_pipe_cloexec(er, err)) return NULL;
    char **env = daemon_env(dir);
    char *argv[] = {MESH_MPS_CONTROL, NULL};
    mesh_spawn_fd fds[] = {{in[0], 0}, {out[1], 1}, {er[1], 2}};
    pid_t pid;
    char *why = NULL;
    bool ok = mesh_spawn(argv, env, fds, 3, &pid, &why);
    free_env(env);
    close(in[0]);
    close(out[1]);
    close(er[1]);
    if (!ok) {
        /* "posix_spawn nvidia-cuda-mps-control: No such file..." -> the io error part */
        const char *e = strstr(why, ": ");
        mesh_errf(err, "mps: %s: %s", MESH_MPS_CONTROL, e ? e + 2 : why);
        free(why);
        close(in[1]);
        close(out[0]);
        close(er[0]);
        return NULL;
    }
    char *line = mesh_fmt("%s\n", req);
    ssize_t w = write(in[1], line, strlen(line));
    (void)w;
    free(line);
    close(in[1]);
    uint64_t t0 = mps_now_ns();
    int st = 0;
    for (;;) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) break;
        if (r < 0 || mps_now_ns() - t0 > 5000000000u) {
            kill(pid, SIGKILL);
            waitpid(pid, &st, 0);
            close(out[0]);
            close(er[0]);
            mesh_errf(err, "mps: `%s` got no answer in 5 s", req);
            return NULL;
        }
        mps_sleep_us(1000);
    }
    mesh_text o = {0}, e = {0};
    read_all(out[0], &o);
    read_all(er[0], &e);
    close(out[0]);
    close(er[0]);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) {
        char *d = mps_status_display(st);
        const char *s = mesh_text_str(&e);
        size_t n = e.len;
        mesh_trim(&s, &n);
        mesh_errf(err, "mps: `%s`: %s %.*s", req, d, (int)n, s);
        free(d);
        mesh_text_free(&o);
        mesh_text_free(&e);
        return NULL;
    }
    const char *s = mesh_text_str(&o);
    size_t n = o.len;
    mesh_trim(&s, &n);
    char *r = mesh_strndup(s, n);
    mesh_text_free(&o);
    mesh_text_free(&e);
    return r;
}

pid_t mps_daemon_pid(const char *dir) {
    char *p = mesh_fmt("%s/pipe/nvidia-cuda-mps-control.pid", dir);
    FILE *f = fopen(p, "r");
    free(p);
    if (!f) return 0;
    long v = 0;
    int n = fscanf(f, "%ld", &v);
    fclose(f);
    return n == 1 && v > 0 ? (pid_t)v : 0;
}

static bool exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void rm_rf(const char *p) {
    DIR *d = opendir(p);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            char *c = mesh_fmt("%s/%s", p, e->d_name);
            struct stat st;
            if (lstat(c, &st) == 0 && S_ISDIR(st.st_mode)) rm_rf(c);
            else unlink(c);
            free(c);
        }
        closedir(d);
    }
    rmdir(p);
}

/* A meshd killed with SIGKILL cannot quit its daemon: such daemons under the
 * default dirs, whose meshd is gone, are quit here. */
static void reap_stale(void) {
    DIR *d = opendir("/tmp");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "meshd-mps.", 10) != 0) continue;
        char *end;
        long pid = strtol(e->d_name + 10, &end, 10);
        if (*end || pid <= 0) continue;
        char *comm = mesh_fmt("/proc/%ld/comm", pid), buf[64] = {0};
        FILE *f = fopen(comm, "r");
        free(comm);
        bool alive = false;
        if (f) {
            if (fgets(buf, sizeof buf, f)) alive = strncmp(buf, "meshd", 5) == 0 && (buf[5] == '\n' || buf[5] == 0);
            fclose(f);
        }
        char *dir = mesh_fmt("/tmp/%s", e->d_name), *ctl = mesh_fmt("%s/pipe/control", dir);
        bool has_ctl = exists(ctl);
        free(ctl);
        if (alive || !has_ctl) {
            free(dir);
            continue;
        }
        pid_t dp = mps_daemon_pid(dir);
        if (dp && kill(dp, 0) == 0) {
            char *err = NULL, *r = mps_control(dir, "quit", &err);
            if (r) mesh_info("mps: quit the daemon %d of meshd %ld, which is gone (%s)", (int)dp, pid, dir);
            else {
                mesh_error("mps: stale daemon %d in %s: %s", (int)dp, dir, err);
                free(err);
                free(dir);
                continue;
            }
            free(r);
        } else {
            mesh_info("mps: removing %s: its meshd and daemon are gone", dir);
        }
        rm_rf(dir);
        free(dir);
    }
    closedir(d);
}

bool mps_start_daemon(const char *dir, bool temporary, char **err) {
    reap_stale();
    char *pipe_dir = mesh_fmt("%s/pipe", dir), *log_dir = mesh_fmt("%s/log", dir), *ctl = mesh_fmt("%s/pipe/control", dir);
    bool ok = mkdir_private(dir, err) && mkdir_private(pipe_dir, err) && mkdir_private(log_dir, err);
    if (ok && exists(ctl)) ok = mesh_errf(err, "mps: %s already has a control pipe (another daemon?); refusing", dir);
    if (ok) {
        char **env = daemon_env(dir);
        char *argv[] = {MESH_MPS_CONTROL, "-d", NULL};
        int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
        mesh_spawn_fd fds[] = {{devnull, 0}, {devnull, 1}, {2, 2}};
        pid_t pid;
        char *why = NULL;
        ok = mesh_spawn(argv, env, fds, 3, &pid, &why);
        free_env(env);
        close(devnull);
        if (!ok) {
            const char *e = strstr(why, ": ");
            mesh_errf(err, "mps: cannot run %s: %s; refusing (--isolation mps needs it)", MESH_MPS_CONTROL, e ? e + 2 : why);
            free(why);
        } else {
            int st;
            waitpid(pid, &st, 0);
            if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) {
                char *d = mps_status_display(st);
                ok = mesh_errf(err, "mps: %s -d: %s", MESH_MPS_CONTROL, d);
                free(d);
            }
        }
    }
    if (ok) {
        mps_set_daemon(dir, temporary);
        uint64_t t0 = mps_now_ns();
        while (!exists(ctl)) {
            if (mps_now_ns() - t0 > 5000000000u) {
                ok = mesh_errf(err, "mps: no control pipe in %s after 5 s", dir);
                break;
            }
            mps_sleep_us(5000);
        }
    }
    if (ok) {
        char *servers = mps_control(dir, "get_server_list", err);
        if (!servers) ok = false;
        else {
            char *q = mesh_debug_str(servers[0] ? servers : "none yet");
            mesh_info("mps: private control daemon (pid %d) on %s; servers %s", (int)mps_daemon_pid(dir), dir, q);
            free(q);
            free(servers);
        }
    }
    free(pipe_dir);
    free(log_dir);
    free(ctl);
    return ok;
}

void mps_quit_daemon(const char *dir, bool temporary) {
    pid_t pid = mps_daemon_pid(dir);
    char *err = NULL, *r = mps_control(dir, "quit", &err);
    if (!r) {
        mesh_error("mps: quit: %s", err);
        free(err);
    }
    free(r);
    uint64_t t0 = mps_now_ns();
    while (pid && kill(pid, 0) == 0) {
        if (mps_now_ns() - t0 > 5000000000u) {
            mesh_error("mps: control daemon %d still running 5 s after quit", (int)pid);
            return;
        }
        mps_sleep_us(5000);
    }
    if (temporary) rm_rf(dir);
    else {
        char *p = mesh_fmt("%s/pipe", dir);
        rm_rf(p);
        free(p);
    }
    if (temporary) mesh_info("mps: control daemon quit");
    else mesh_info("mps: control daemon quit (logs in %s/log)", dir);
}
