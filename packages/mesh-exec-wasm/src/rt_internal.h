/* What the executor's files share. */
#ifndef MESH_EXEC_WASM_RT_INTERNAL_H
#define MESH_EXEC_WASM_RT_INTERNAL_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

struct mesh_wasm_rt {
    char *cache;            /* --wasm-cache DIR, or NULL */
    _Atomic uint64_t epoch; /* ticks every ms while lanes run */
    _Atomic uint32_t lanes; /* lanes alive (the epoch thread ticks only while some are) */
    _Atomic bool stop;
    pthread_t thread;
};

/* A malloc'd printf. */
char *mesh_wasm_fmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
