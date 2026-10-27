/* @ajax/mesh-log: INFO/ERROR lines with a UTC wall-clock timestamp on stderr
 * through a log thread (mesh-host log.rs): emitting never blocks and never
 * fails.  A line goes into a bounded queue (MESH_LOG_QUEUE lines); when it is
 * full -- stderr is a pipe nobody reads, a slow terminal -- the line is
 * dropped from stderr and counted (S_LOG_DROPPED).  The log thread writes
 * with write(2) and ignores its errors (EPIPE counts as dropped too).  The
 * last MESH_LOG_KEEP lines are kept in memory (meshd's `log` request). */
#ifndef MESH_LOG_H
#define MESH_LOG_H

#include <stddef.h>
#include <stdint.h>

#define MESH_LOG_KEEP 1000
#define MESH_LOG_QUEUE 4096

/* "HH:MM:SS.mmm" (UTC) into buf (at least 13 bytes). */
void mesh_log_stamp(char buf[16]);
/* Queues one line for stderr (dropped and counted if the queue is full) and keeps it. Takes a copy. */
void mesh_log_emit(const char *line);
void mesh_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void mesh_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Lines not written to stderr. */
uint64_t mesh_log_dropped(void);
/* Waits (at most limit_ms) for the queued lines to be written. */
void mesh_log_flush(unsigned limit_ms);
/* Exits once the queued lines are written (at most 500 ms). */
_Noreturn void mesh_log_exit(int code);
/* The last n lines, oldest first: *lines is a malloc'd array of malloc'd strings. */
size_t mesh_log_recent(size_t n, char ***lines);
/* Where lines go (default fd 2); for tests. */
void mesh_log_set_fd(int fd);

#endif
