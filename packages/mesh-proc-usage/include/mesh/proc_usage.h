/* @ajax/mesh-proc-usage: a process's CPU ns (user + system, every thread) and
 * resident bytes (mesh-host native.rs usage), read from /proc/<pid>/stat.
 * macOS (proc_pidinfo) is not ported. */
#ifndef MESH_PROC_USAGE_H
#define MESH_PROC_USAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

bool mesh_proc_usage(pid_t pid, uint64_t *cpu_ns, uint64_t *rss);

#endif
