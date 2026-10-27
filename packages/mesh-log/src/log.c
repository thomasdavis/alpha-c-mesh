/* log.rs: a bounded queue drained by one thread, a ring of recent lines, counters. */
#include "mesh/log.h"
#include "mesh/text.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static char *queue[MESH_LOG_QUEUE];
static size_t qhead, qlen;
static char *recent[MESH_LOG_KEEP];
static size_t rhead, rlen;
static bool started, thread_failed;
static atomic_uint_fast64_t dropped, queued, done;
static atomic_int out_fd = 2;

void mesh_log_set_fd(int fd) { atomic_store(&out_fd, fd); }

void mesh_log_stamp(char buf[16]) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    unsigned s = (unsigned)(ts.tv_sec % 86400), ms = (unsigned)(ts.tv_nsec / 1000000) % 1000;
    snprintf(buf, 16, "%02u:%02u:%02u.%03u", s / 3600 % 24, s / 60 % 60, s % 60, ms);
}

/* The whole line on the fd, or false on an error (EPIPE included; SIGPIPE is the program's to ignore). */
static bool write_all(int fd, const char *b, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t k = write(fd, b + off, n - off);
        if (k > 0) {
            off += (size_t)k;
            continue;
        }
        if (k < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

static void *drain(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mu);
        while (qlen == 0) pthread_cond_wait(&cv, &mu);
        char *l = queue[qhead];
        qhead = (qhead + 1) % MESH_LOG_QUEUE;
        qlen--;
        pthread_mutex_unlock(&mu);
        size_t n = strlen(l);
        l[n] = '\n'; /* the slot has room: see emit */
        if (!write_all(atomic_load(&out_fd), l, n + 1)) atomic_fetch_add(&dropped, 1);
        free(l);
        atomic_fetch_add(&done, 1);
    }
    return NULL;
}

void mesh_log_emit(const char *line) {
    size_t n = strlen(line);
    char *keep = mesh_strndup(line, n);
    char *q = mesh_alloc(n + 2); /* the line, its '\n', NUL */
    memcpy(q, line, n);
    pthread_mutex_lock(&mu);
    if (rlen == MESH_LOG_KEEP) {
        free(recent[rhead]);
        recent[rhead] = keep;
        rhead = (rhead + 1) % MESH_LOG_KEEP;
    } else {
        recent[(rhead + rlen++) % MESH_LOG_KEEP] = keep;
    }
    if (!started) {
        started = true;
        pthread_t t;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        thread_failed = pthread_create(&t, &at, drain, NULL) != 0;
        pthread_attr_destroy(&at);
    }
    bool ok = !thread_failed && qlen < MESH_LOG_QUEUE;
    if (ok) {
        queue[(qhead + qlen++) % MESH_LOG_QUEUE] = q;
        pthread_cond_signal(&cv);
    }
    pthread_mutex_unlock(&mu);
    if (ok) atomic_fetch_add(&queued, 1);
    else {
        free(q);
        atomic_fetch_add(&dropped, 1);
    }
}

static void emitv(const char *level, const char *fmt, va_list ap) {
    char st[16];
    mesh_log_stamp(st);
    mesh_text t = {0};
    mesh_text_printf(&t, "%s %s ", st, level);
    mesh_text_vprintf(&t, fmt, ap);
    mesh_log_emit(mesh_text_str(&t));
    mesh_text_free(&t);
}

void mesh_info(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emitv("INFO ", fmt, ap);
    va_end(ap);
}

void mesh_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emitv("ERROR", fmt, ap);
    va_end(ap);
}

uint64_t mesh_log_dropped(void) { return (uint64_t)atomic_load(&dropped); }

void mesh_log_flush(unsigned limit_ms) {
    struct timespec t0, now, nap = {0, 1000000};
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        if (atomic_load(&done) >= atomic_load(&queued)) return;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long ms = (long)(now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000;
        if (ms >= (long)limit_ms) return;
        nanosleep(&nap, NULL);
    }
}

_Noreturn void mesh_log_exit(int code) {
    mesh_log_flush(500);
    exit(code);
}

size_t mesh_log_recent(size_t n, char ***lines) {
    pthread_mutex_lock(&mu);
    size_t k = n < rlen ? n : rlen;
    *lines = mesh_alloc((k ? k : 1) * sizeof **lines);
    for (size_t i = 0; i < k; i++) (*lines)[i] = mesh_strdup(recent[(rhead + rlen - k + i) % MESH_LOG_KEEP]);
    pthread_mutex_unlock(&mu);
    return k;
}
