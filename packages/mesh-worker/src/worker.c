/* lib.rs's run: the process side of the native worker protocol. */
#include "mesh/worker.h"

#include "mesh/abi/base.h"
#include "mesh/abi/control_block.h"
#include "mesh/abi/header.h"
#include "mesh/abi/native.h"
#include "mesh/abi/program_table.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

_Noreturn void mesh_worker_panic(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("mesh-worker: panic: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
    _exit(101);
}

static _Noreturn void fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fputs("mesh-worker: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}

static mesh_mailbox map_mailbox(void) {
    int fd = MESH_NATIVE_MAILBOX_FD;
    struct stat st;
    if (fstat(fd, &st) != 0) fail("fd %d (the mailbox): %s (os error %d)", fd, strerror(errno), errno);
    if ((size_t)st.st_size < MESH_MAILBOX_BYTES)
        fail("fd %d: %lld bytes, not a %d-byte mailbox", fd, (long long)st.st_size, MESH_MAILBOX_BYTES);
    void *p = mmap(NULL, MESH_MAILBOX_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) fail("mmap of the mailbox: %s (os error %d)", strerror(errno), errno);
    close(fd);
    mesh_mailbox mb = mesh_mailbox_new(p, 0);
    if (mesh_mailbox_get(mb, MESH_H_MAGIC) != MESH_MAGIC || mesh_mailbox_get(mb, MESH_H_VERSION) != MESH_VERSION)
        fail("the mailbox has magic 0x%x version %u", mesh_mailbox_get(mb, MESH_H_MAGIC), mesh_mailbox_get(mb, MESH_H_VERSION));
    return mb;
}

/* Commands from meshd, from the command thread to the main loop. */
typedef struct {
    bool exit;
    uint32_t gen, slot;
} cmd;

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static cmd queue[64];
static size_t qhead, qlen;

static void send(cmd c) {
    pthread_mutex_lock(&mu);
    if (qlen < 64) queue[(qhead + qlen++) % 64] = c;
    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mu);
}

static cmd receive(void) {
    pthread_mutex_lock(&mu);
    while (qlen == 0) pthread_cond_wait(&cv, &mu);
    cmd c = queue[qhead];
    qhead = (qhead + 1) % 64;
    qlen--;
    pthread_mutex_unlock(&mu);
    return c;
}

/* One command line: "run <gen> <slot>" or "exit" (anything else is ignored). */
static void command(char *line) {
    char w0[16], w1[32], w2[32], extra[8];
    int n = sscanf(line, "%15s %31s %31s %7s", w0, w1, w2, extra);
    if (n == 1 && strcmp(w0, "exit") == 0) {
        atomic_store(&mesh_worker_stop_all, true);
        send((cmd){true, 0, 0});
    } else if (n == 3 && strcmp(w0, "run") == 0) {
        char *e1, *e2;
        errno = 0;
        unsigned long g = strtoul(w1, &e1, 10), s = strtoul(w2, &e2, 10);
        if (*e1 || *e2 || errno || g > UINT32_MAX || s > UINT32_MAX || w1[0] == '-' || w2[0] == '-') return;
        send((cmd){false, (uint32_t)g, (uint32_t)s});
    }
}

/* Reads stdin with read(2), not stdio: a node thread that calls exit() flushes
 * every FILE, which would wait forever on a reader blocked inside fgets. */
static void *commands(void *arg) {
    (void)arg;
    char buf[512];
    size_t len = 0;
    for (;;) {
        ssize_t k = read(0, buf + len, sizeof buf - 1 - len);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        len += (size_t)k;
        buf[len] = 0;
        char *nl;
        while ((nl = memchr(buf, '\n', len))) {
            *nl = 0;
            command(buf);
            size_t used = (size_t)(nl + 1 - buf);
            memmove(buf, nl + 1, len - used);
            len -= used;
            buf[len] = 0;
        }
        if (len == sizeof buf - 1) len = 0; /* an overlong line: dropped */
    }
    /* meshd is gone (or closed our stdin): halt and leave. */
    atomic_store(&mesh_worker_stop_all, true);
    send((cmd){true, 0, 0});
    return NULL;
}

_Noreturn void mesh_worker_run(const mesh_worker_program *p) {
    const char *abi = getenv(MESH_NATIVE_ENV_ABI);
    char want[16];
    snprintf(want, sizeof want, "%u", (unsigned)MESH_NATIVE_ABI);
    if (!abi) fail("a mesh native worker: meshd starts it (load it from a catalog; %s is not set)", MESH_NATIVE_ENV_ABI);
    if (strcmp(abi, want) != 0) fail("meshd speaks native worker ABI %s, this program %s: rebuild it", abi, want);
    mesh_mailbox mb = map_mailbox();
    FILE *status = fdopen(MESH_NATIVE_STATUS_FD, "w");
    if (!status) exit(0);
    pthread_t t;
    if (pthread_create(&t, NULL, commands, NULL) != 0) mesh_worker_panic("a command thread");
    pthread_detach(t);
    if (fprintf(status, "ready %u\n", (unsigned)MESH_NATIVE_ABI) < 0 || fflush(status) != 0) exit(0);
    static atomic_bool stop;
    for (;;) {
        cmd c = receive();
        if (c.exit) exit(0);
        if (c.slot >= MESH_PROG_SLOTS) continue;
        mesh_worker_run_nodes(mb, c.slot, p, &stop);
        fprintf(status, "halted %u\n", (unsigned)c.gen);
        fflush(status);
        if (mesh_mailbox_get(mb, mesh_prog_ctl(c.slot) + MESH_P_SHUTDOWN) == MESH_SHUTDOWN_EXIT || atomic_load(&mesh_worker_stop_all))
            exit(0);
    }
}
