/* The native executor's internals, shared by its source files. */
#ifndef MESH_NATIVE_INTERNAL_H
#define MESH_NATIVE_INTERNAL_H

#include "mesh/cpuuse.h"
#include "mesh/log.h"
#include "mesh/mailbox.h"
#include "mesh/native.h"
#include "mesh/proc_usage.h"
#include "mesh/spawn.h"
#include "mesh/text.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

/* How long proc_kill waits for a SIGKILLed process to be reaped. */
#define KILL_REAP_MS 500u

uint64_t mesh_native_now_ns(void);
void mesh_native_sleep_us(unsigned us);

/* One worker process (refcounted: the host's list, an image's run, its reader threads). */
typedef struct proc {
    atomic_int refs;
    pid_t pid;
    char *name;
    pthread_mutex_t stdin_mu;
    int stdin_fd;
    /* What the status pipe told. */
    _Atomic uint32_t ready, halted_gen;
    atomic_bool eof;
    _Atomic uint32_t outs_done; /* stdout and stderr readers at EOF (2: every line is in the tail) */
    pthread_mutex_t tail_mu;
    char *tail[MESH_NATIVE_TAIL];
    size_t tail_head, tail_len;
    pthread_mutex_t status_mu;
    bool exited;
    int status;
    pthread_mutex_t sample_mu;
    uint64_t sample_t, sample_cpu, sample_rss;
    float sample_pct;
    uint32_t limits;
    atomic_bool exit_sent;
} proc;

void proc_ref(proc *p);
void proc_unref(proc *p);
bool proc_send(proc *p, const char *line);
void proc_tell_exit(proc *p);
bool proc_exited(proc *p, int *status);
void proc_kill(proc *p);
char *proc_tail_text(proc *p);
void proc_sample(proc *p, uint64_t *cpu, uint64_t *rss, float *pct);
void proc_last(proc *p, uint64_t *cpu, uint64_t *rss, float *pct);
void proc_retire(proc *p, mesh_native_host *h);
void mesh_native_status_line(proc *p, const char *l);

struct mesh_native_host {
    mesh_mailbox mb;
    mesh_shm shm;
    char *logs;
    mesh_worker_cfg cfg;
    pthread_mutex_t mu;
    proc **procs;
    size_t nprocs;
    _Atomic uint32_t spawns, crashes;
};

/* What a spawn needs (refcounted: shared with the thread that spawns). */
typedef struct {
    atomic_int refs;
    char *name;
    char *bin;
    mesh_native_limits limits;
    bool gpu; /* manifest device = "gpu" */
    _Atomic uint32_t spawns;
} spec;

void host_forget(mesh_native_host *h, proc *p);
/* A new worker process for s, up to its `ready` (blocks: call it off the driver thread). */
bool host_spawn(mesh_native_host *h, spec *s, proc **out, char **err);

static inline _Noreturn void mesh_worker_fatal_thread(void) {
    mesh_error("native: thread: cannot start one");
    abort();
}

#endif
