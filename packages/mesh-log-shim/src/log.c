/* A synchronous stand-in for mesh-log: timestamped lines on a fd, the last MESH_LOG_KEEP kept. */
#include "mesh/log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char *keep[MESH_LOG_KEEP];
static size_t head, count;
static int out_fd = 2;

void mesh_log_stamp(char buf[16]) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned s = (unsigned)(tv.tv_sec % 86400), ms = (unsigned)(tv.tv_usec / 1000) % 1000u;
    snprintf(buf, 16, "%02u:%02u:%02u.%03u", (s / 3600) % 24u, s / 60 % 60, s % 60, ms);
}

void mesh_log_emit(const char *line) {
    size_t n = strlen(line);
    char *c = malloc(n + 2);
    if (!c) return;
    memcpy(c, line, n);
    pthread_mutex_lock(&mu);
    if (count == MESH_LOG_KEEP) { free(keep[head]); keep[head] = NULL; head = (head + 1) % MESH_LOG_KEEP; count--; }
    c[n] = 0;
    keep[(head + count) % MESH_LOG_KEEP] = c;
    count++;
    pthread_mutex_unlock(&mu);
    char *l = malloc(n + 1);
    if (!l) return;
    memcpy(l, line, n);
    l[n] = '\n';
    ssize_t w = write(out_fd, l, n + 1);
    (void)w;
    free(l);
}

static void emit(const char *level, const char *fmt, va_list ap) {
    char st[16];
    mesh_log_stamp(st);
    va_list c;
    va_copy(c, ap);
    int n = vsnprintf(NULL, 0, fmt, c);
    va_end(c);
    char *body = malloc((size_t)(n > 0 ? n : 0) + 1);
    if (!body) return;
    vsnprintf(body, (size_t)(n > 0 ? n : 0) + 1, fmt, ap);
    size_t total = strlen(st) + strlen(level) + strlen(body) + 3;
    char *line = malloc(total);
    if (line) {
        snprintf(line, total, "%s %s %s", st, level, body);
        mesh_log_emit(line);
        free(line);
    }
    free(body);
}

void mesh_info(const char *fmt, ...) { va_list ap; va_start(ap, fmt); emit("INFO ", fmt, ap); va_end(ap); }
void mesh_error(const char *fmt, ...) { va_list ap; va_start(ap, fmt); emit("ERROR", fmt, ap); va_end(ap); }
uint64_t mesh_log_dropped(void) { return 0; }
void mesh_log_flush(unsigned limit_ms) { (void)limit_ms; }
_Noreturn void mesh_log_exit(int code) { exit(code); }
void mesh_log_set_fd(int fd) { out_fd = fd; }

size_t mesh_log_recent(size_t n, char ***lines) {
    pthread_mutex_lock(&mu);
    size_t k = n < count ? n : count;
    *lines = malloc((k ? k : 1) * sizeof **lines);
    for (size_t i = 0; i < k; i++) {
        const char *s = keep[(head + count - k + i) % MESH_LOG_KEEP];
        size_t m = strlen(s);
        (*lines)[i] = malloc(m + 1);
        memcpy((*lines)[i], s, m + 1);
    }
    pthread_mutex_unlock(&mu);
    return k;
}
