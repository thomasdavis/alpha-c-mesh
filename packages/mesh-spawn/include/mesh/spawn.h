/* @ajax/mesh-spawn: starting worker processes (mesh-host native.rs spawn,
 * pipe, describe, exit_word).  posix_spawn, not fork: a fork of meshd copies
 * the page tables of its GPU mappings under the mm lock (5-12 ms measured on
 * dgx-00, stalling every other thread); posix_spawn does not. */
#ifndef MESH_SPAWN_H
#define MESH_SPAWN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* A pipe, both ends close-on-exec: fds[0] read, fds[1] write. */
bool mesh_pipe_cloexec(int fds[2], char **err);

/* One (parent fd, child fd) for mesh_spawn. */
typedef struct {
    int parent, child;
} mesh_spawn_fd;

/* posix_spawnp of argv (NULL-terminated) with this process's environment plus
 * env ("K=V" entries, NULL-terminated, overriding), each fds pair dup2'd in
 * (every other descriptor stays closed: they are close-on-exec), in a process
 * group of its own (a terminal's ^C goes to meshd).  False with *err:
 * "posix_spawn <argv0>: <why>". */
bool mesh_spawn(char *const *argv, char *const *env, const mesh_spawn_fd *fds, size_t nfds, pid_t *pid, char **err);

/* "exit 3", "signal 9 (Killed)", "signal 6 (Aborted), core dumped" (malloc'd). */
char *mesh_describe_status(int status);
/* P_EXIT's encoding: status << 8 | signal. */
uint32_t mesh_exit_word(int status);
/* A wait status that is a clean exit 0. */
bool mesh_status_success(int status);

#endif
