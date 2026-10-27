/* A whole CPU-only meshd (--backend wasm) in a child process, on a catalog holding the real hello
 * program (programs/hello-wasm, built by sexc), driven over its control and HTTP ports: autoload,
 * tasks, stop/start, reset, unload/load, peek, the views, --state, SIGINT and exit 0. Also: the
 * CUDA backend without a GPU fails cleanly (exit 1), and a bad flag exits 2. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "meshd.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"
#include "mesh/backend.h"

#include <limits.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int free_port(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (struct sockaddr *)&sa, sizeof sa);
    socklen_t sl = sizeof sa;
    getsockname(fd, (struct sockaddr *)&sa, &sl);
    close(fd);
    return ntohs(sa.sin_port);
}

static int conn(int port) {
    for (int i = 0; i < 300; i++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET;
        sa.sin_port = htons((uint16_t)port);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) return fd;
        close(fd);
        struct timespec t = {0, 20000000};
        nanosleep(&t, NULL);
    }
    return -1;
}

static int FD;
static char *rline(void) {
    static char b[4096];
    size_t i = 0;
    while (i < sizeof b - 1 && read(FD, b + i, 1) == 1) if (b[i++] == '\n') break;
    b[i] = 0;
    return b;
}
static char *ask(const char *req) {
    if (write(FD, req, strlen(req)) < 0 || write(FD, "\n", 1) < 0) abort();
    return rline();
}
static char *frame(const char *req, size_t *len) {
    if (write(FD, req, strlen(req)) < 0 || write(FD, "\n", 1) < 0) abort();
    uint8_t h[4];
    for (size_t got = 0; got < 4;) { ssize_t k = read(FD, h + got, 4 - got); if (k <= 0) return NULL; got += (size_t)k; }
    size_t n = (size_t)h[0] | (size_t)h[1] << 8 | (size_t)h[2] << 16 | (size_t)h[3] << 24;
    char *b = malloc(n + 1);
    for (size_t got = 0; got < n;) { ssize_t k = read(FD, b + got, n - got); if (k <= 0) break; got += (size_t)k; }
    b[n] = 0;
    if (len) *len = n;
    return b;
}

static pid_t run(char **argv, int argc, const char *logpath) {
    pid_t pid = fork();
    if (pid == 0) {
        int fd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        dup2(fd, 2);
        dup2(fd, 1);
        meshd_main(argc, argv);
    }
    return pid;
}

static int wait_exit(pid_t pid) {
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

int main(void) {
    char *wasm = mesh_kit_artifact("ajax-mesh-prog-hello-wasm");
    if (!wasm) {
        fprintf(stderr, "meshd test: programs/hello-wasm is not built (run `sexc test` for the workspace); skipped\n");
        return 0;
    }
    char root[128], cat[192], path[256], state[192], logs[192], logf[192];
    snprintf(root, sizeof root, "/tmp/meshd-e2e-%d", (int)getpid());
    snprintf(cat, sizeof cat, "%s/programs", root);
    snprintf(state, sizeof state, "%s/state", root);
    snprintf(logs, sizeof logs, "%s/logs", root);
    snprintf(logf, sizeof logf, "%s/meshd.log", root);
    mkdir(root, 0700);
    mkdir(cat, 0700);
    snprintf(path, sizeof path, "%s/hello", cat);
    mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/hello/program.toml", cat);
    FILE *f = fopen(path, "w");
    fputs("# hello: task = arg * 3 + 7\nname       = \"hello\"\nabi        = 6\nview_bytes = 0x1000\nview_abi   = 1\nsnap_bytes = 0x40\nnodes      = 2\n"
          "policy     = \"on_demand\"\ncounters   = [\"tasks\", \"inits\", \"\", \"\"]\nfields     = [\"magic:u32:0x0\", \"last_arg:u32:0x4\", \"last_result:u32:0x8\"]\n"
          "autostart  = true\nbackends   = [\"cuda\", \"metal\", \"wasm\"]\n",
          f);
    fclose(f);
    snprintf(path, sizeof path, "%s/hello/hello.wasm", cat);
    FILE *in = fopen(wasm, "rb"), *out = fopen(path, "wb");
    char buf[65536];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, k, out);
    fclose(in);
    fclose(out);
    free(wasm);

    /* A native worker program (apps/mesh-test-worker): its binary for this target, a source hash, param 2. */
    char self[PATH_MAX], fixture[PATH_MAX + 64];
    ssize_t sn = readlink("/proc/self/exe", self, sizeof self - 1);
    self[sn > 0 ? sn : 0] = 0;
    char *tp = strstr(self, "/test/");
    if (tp) *tp = 0;
    snprintf(fixture, sizeof fixture, "%s/bin/mesh-test-worker", self);
    bool native = access(fixture, X_OK) == 0;
    if (native) {
        snprintf(path, sizeof path, "%s/nt", cat);
        mkdir(path, 0700);
        snprintf(path, sizeof path, "%s/nt/%s", cat, mesh_native_target());
        mkdir(path, 0700);
        snprintf(path, sizeof path, "%s/nt/program.toml", cat);
        f = fopen(path, "w");
        fputs("name = \"nt\"\nabi = 6\nbackends = [\"native\"]\nview_bytes = 0x1000\nnodes = 2\nparam = 2\n", f);
        fclose(f);
        snprintf(path, sizeof path, "%s/nt/source.hash", cat);
        f = fopen(path, "w");
        fputs("0123456789abcdef\n", f);
        fclose(f);
        snprintf(path, sizeof path, "%s/nt/%s/nt", cat, mesh_native_target());
        in = fopen(fixture, "rb");
        out = fopen(path, "wb");
        while ((k = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, k, out);
        fclose(in);
        fclose(out);
        chmod(path, 0755);
    } else fprintf(stderr, "meshd test: %s is not built; the native part is skipped\n", fixture);

    /* No GPU here: the CUDA backend refuses to start (exit 1); a bad flag is exit 2. */
    int cp = free_port(), hp = free_port();
    char ctl[32], http[32];
    snprintf(ctl, sizeof ctl, "127.0.0.1:%d", cp);
    snprintf(http, sizeof http, "127.0.0.1:%d", hp);
    char *cuda[] = {"meshd", "--backend", "cuda", "--nvjitlink", "/nonexistent", "--programs", cat, "--control", ctl, "--http", http, NULL};
    CHECK_I(wait_exit(run(cuda, 11, logf)), 1);
    char *badf[] = {"meshd", "--bogus", NULL};
    CHECK_I(wait_exit(run(badf, 2, logf)), 2);

    char *argv[] = {"meshd", "--backend", "wasm", "--programs", cat, "--autoload", "--state", state, "--control", ctl, "--http", http, "--logs", logs, NULL};
    pid_t pid = run(argv, 14, logf);
    FD = conn(cp);
    CHECK(FD >= 0);
    /* The autoloaded hello: running once its OP_INIT is done. */
    char *r = NULL;
    for (int i = 0; i < 100; i++) {
        r = ask("task hello 0 5");
        if (strcmp(r, "ok 22\n") == 0) break;
        struct timespec t = {0, 50000000};
        nanosleep(&t, NULL);
    }
    CHECK_S(r, "ok 22\n");
    CHECK_S(ask("task hello 0 0x10"), "ok 55\n");
    size_t n;
    char *b = frame("peek hello 0 12", &n);
    CHECK(n == 12 && memcmp(b, "HELO", 4) == 0 && (uint8_t)b[4] == 16 && (uint8_t)b[8] == 55);
    free(b);
    b = frame("programs", NULL);
    CHECK(strncmp(b, "0 hello running 2 1 on_demand ", 30) == 0);
    CHECK(strstr(b, " 0x100000+0x1000 kind wasm lanes ") != NULL);
    CHECK(strstr(b, "\nfields hello magic:u32:0x0 last_arg:u32:0x4 last_result:u32:0x8\ncounters hello tasks inits - -\n") != NULL);
    free(b);
    r = ask("stop hello");
    CHECK(strncmp(r, "ok ", 3) == 0 && strstr(r, " us\n"));
    CHECK_S(ask("task hello 0 1"), "err hello is not running\n");
    r = ask("start hello");
    CHECK(strncmp(r, "ok ", 3) == 0 && strstr(r, " us\n"));
    CHECK_S(ask("task hello 0 2"), "ok 13\n");
    r = ask("reset hello");
    CHECK(strncmp(r, "ok blink ", 9) == 0);
    CHECK_S(ask("load hello"), "err name exists: hello is loaded\n");
    CHECK_S(ask("unload hello"), "ok\n");
    r = ask("load hello");
    CHECK(strncmp(r, "ok slot 0 blink ", 16) == 0);
    for (int i = 0; i < 100; i++) {
        r = ask("task hello 0 9");
        if (strcmp(r, "ok 34\n") == 0) break;
        struct timespec t = {0, 20000000};
        nanosleep(&t, NULL);
    }
    CHECK_S(r, "ok 34\n");
    b = frame("catalog", NULL);
    CHECK_S(b, native ? "hello\nnt\n" : "hello\n");
    free(b);
    if (native) {
        /* A native worker beside the wasm lanes: its own process, its counts in `programs`, a crash FAILs only it. */
        r = ask("load nt");
        CHECK(strncmp(r, "ok slot 1 blink ", 16) == 0);
        r = ask("start nt"); /* no autostart: loaded stopped */
        CHECK(strncmp(r, "ok ", 3) == 0 && strstr(r, " us\n"));
        for (int i = 0; i < 200; i++) {
            r = ask("task nt 0 5");
            if (strcmp(r, "ok 2022\n") == 0) break;
            struct timespec t = {0, 20000000};
            nanosleep(&t, NULL);
        }
        CHECK_S(r, "ok 2022\n");
        b = frame("programs", NULL);
        CHECK(strstr(b, "\n1 nt running 2 1 on_demand ") && strstr(b, " kind native pid ") && strstr(b, " spawns 1 crashes 0 exit 0/0 "));
        free(b);
        r = ask("task nt 9 0");
        CHECK(strncmp(r, "err ", 4) == 0);
        for (int i = 0; i < 200; i++) {
            b = frame("programs", NULL);
            bool failed = strstr(b, "\n1 nt failed ") != NULL;
            free(b);
            if (failed) break;
            struct timespec t = {0, 20000000};
            nanosleep(&t, NULL);
        }
        b = frame("programs", NULL);
        CHECK(strstr(b, "\n1 nt failed ") && strstr(b, " crashes 1 "));
        free(b);
        CHECK_S(ask("task hello 0 1"), "ok 10\n");
        CHECK_S(ask("unload nt"), "ok\n");
    }
    b = frame("view pipes", NULL);
    CHECK_S(b, "pipes system=4294967295 inboxes=0\n");
    free(b);
    CHECK_S(ask("fleet"), "err not in a fleet (single meshd: --cluster / --peer)\n");
    CHECK_S(ask("jobs"), "err the jobs thread is gone\n");
    b = frame("http /status", NULL);
    CHECK_S(b, "err no program \"http\" (`programs` lists them)\n");
    free(b);
    b = frame("snap", &n);
    CHECK(n > 0x3000);
    free(b);
    /* The HTTP port answers the views itself. */
    int h = conn(hp);
    const char *get = "GET /fleet HTTP/1.0\r\n\r\n";
    if (write(h, get, strlen(get)) < 0) abort();
    char resp[1024] = "";
    size_t got = 0;
    ssize_t kk;
    while (got < sizeof resp - 1 && (kk = read(h, resp + got, sizeof resp - 1 - got)) > 0) got += (size_t)kk;
    resp[got] = 0;
    close(h);
    CHECK(strncmp(resp, "HTTP/1.0 200 OK\r\n", 17) == 0 && strstr(resp, "fleet system=4294967295 n=1 role=single"));
    close(FD);
    /* SIGINT: quiesce, halt, --state written, exit 0. */
    kill(pid, SIGINT);
    CHECK_I(wait_exit(pid), 0);
    f = fopen(state, "r");
    char line[256] = "", l2[256] = "";
    if (f) {
        if (!fgets(line, sizeof line, f) || !fgets(l2, sizeof l2, f)) l2[0] = 0;
        fclose(f);
    }
    CHECK_S(l2, "hello hello 1\n");
    f = fopen(logf, "r");
    char all[65536];
    size_t la = f ? fread(all, 1, sizeof all - 1, f) : 0;
    all[la] = 0;
    if (f) fclose(f);
    CHECK(strstr(all, "INFO  programs halted, exiting") && strstr(all, "INFO  exit 0"));
    if (mesh_check_failures) fprintf(stderr, "meshd's log:\n%s\n", all);
    /* Leave nothing behind. */
    char rm[300];
    snprintf(rm, sizeof rm, "%s/hello/program.toml", cat);
    unlink(rm);
    snprintf(rm, sizeof rm, "%s/hello/hello.wasm", cat);
    unlink(rm);
    snprintf(rm, sizeof rm, "%s/hello", cat);
    rmdir(rm);
    if (native) {
        snprintf(rm, sizeof rm, "%s/nt/%s/nt", cat, mesh_native_target());
        unlink(rm);
        snprintf(rm, sizeof rm, "%s/nt/%s", cat, mesh_native_target());
        rmdir(rm);
        snprintf(rm, sizeof rm, "%s/nt/program.toml", cat);
        unlink(rm);
        snprintf(rm, sizeof rm, "%s/nt/source.hash", cat);
        unlink(rm);
        snprintf(rm, sizeof rm, "%s/nt", cat);
        rmdir(rm);
        snprintf(rm, sizeof rm, "%s/nt.log", logs);
        unlink(rm);
    }
    rmdir(cat);
    unlink(state);
    unlink(logf);
    rmdir(logs);
    rmdir(root);
    return mesh_check_done();
}
