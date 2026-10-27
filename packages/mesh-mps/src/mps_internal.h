/* mesh-mps's internals, shared by its source files. */
#ifndef MESH_MPS_INTERNAL_H
#define MESH_MPS_INTERNAL_H

#include "mesh/log.h"
#include "mesh/mps.h"
#include "mesh/mps_wire.h"
#include "mesh/spawn.h"
#include "mesh/text.h"

#include <pthread.h>
#include <stdatomic.h>

char *mps_os_error(int e);
uint64_t mps_now_ns(void);
void mps_sleep_us(unsigned us);
char *mps_status_display(int st);
/* One request to our control daemon: its reply (trimmed, malloc'd), or NULL with *err. */
char *mps_control(const char *dir, const char *req, char **err);
pid_t mps_daemon_pid(const char *dir);
bool mps_start_daemon(const char *dir, bool temporary, char **err);
void mps_quit_daemon(const char *dir, bool temporary);
/* The daemon's dir while it runs (cleanup quits it). */
void mps_set_daemon(const char *dir, bool temporary);

#endif
