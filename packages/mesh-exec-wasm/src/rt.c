/* The engine (mesh-host/src/wasm.rs `Rt`): the compile cache and the epoch thread. */
#include "mesh/exec_wasm.h"

#include "mesh/abi/fnv.h"
#include "mesh/exec_wasm_cache.h"
#include "mesh/log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rt_internal.h"

char *mesh_wasm_fmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char *s = malloc(n > 0 ? (size_t)n + 1 : 1);
    if (!s) return NULL;
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

static void *epoch_thread(void *arg) {
    mesh_wasm_rt *rt = arg;
    while (!atomic_load(&rt->stop)) {
        if (atomic_load(&rt->lanes) == 0) {
            struct timespec idle = {0, 20000000};
            nanosleep(&idle, NULL);
            continue;
        }
        atomic_fetch_add(&rt->epoch, 1);
        struct timespec tick = {0, (long)MESH_WASM_TICK_NS};
        nanosleep(&tick, NULL);
    }
    return NULL;
}

mesh_wasm_rt *mesh_wasm_rt_new(const char *cache, char **err) {
    if (cache && !mesh_wasm_cache_dir(cache, err)) return NULL;
    mesh_wasm_rt *rt = calloc(1, sizeof *rt);
    if (!rt) {
        if (err) *err = mesh_wasm_fmt("wasm: out of memory");
        return NULL;
    }
    rt->cache = cache ? strdup(cache) : NULL;
    if (pthread_create(&rt->thread, NULL, epoch_thread, rt) != 0) {
        if (err) *err = mesh_wasm_fmt("wasm: epoch thread: cannot start it");
        free(rt->cache);
        free(rt);
        return NULL;
    }
    return rt;
}

void mesh_wasm_rt_free(mesh_wasm_rt *rt) {
    if (!rt) return;
    atomic_store(&rt->stop, true);
    pthread_join(rt->thread, NULL);
    free(rt->cache);
    free(rt);
}

uint64_t mesh_wasm_engine_hash(void) {
    /* What the compiled form depends on: the interpreter and the proposals it runs. */
    char id[96];
    snprintf(id, sizeof id, "alpha-c-mesh wasm-interp 0.1.0 features %x", (unsigned)WASM_F_INTERP);
    return mesh_fnv1a64((const uint8_t *)id, strlen(id));
}

static void evicted(const char *path) {
    mesh_info("wasm: cache over %llu bytes: evicted %s", (unsigned long long)MESH_WASM_CACHE_CAP, path);
}

static double ms_since(const struct timespec *t0) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (double)(t1.tv_sec - t0->tv_sec) * 1e3 + (double)(t1.tv_nsec - t0->tv_nsec) / 1e6;
}

wasm_compiled *mesh_wasm_rt_module(mesh_wasm_rt *rt, const char *name, const uint8_t *wasm, size_t n, double *ms,
                                   const char **how, char **err) {
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    char msg[512];
    char *path = rt->cache ? mesh_wasm_cache_path(rt->cache, wasm, n, mesh_wasm_engine_hash()) : NULL;
    if (path) {
        uint8_t *cached;
        size_t clen;
        if (mesh_wasm_cache_load(path, &cached, &clen)) {
            /* Our own cache: the module's bytes, written below. */
            wasm_compiled *m = clen == n && memcmp(cached, wasm, n) == 0
                                   ? wasm_compile(cached, clen, WASM_F_INTERP, msg, sizeof msg)
                                   : NULL;
            free(cached);
            if (m) {
                *ms = ms_since(&t0);
                *how = "cached";
                free(path);
                return m;
            }
            mesh_error("wasm: %s: cached %s unusable (%s): compiling", name, path, "not this module");
        }
    }
    wasm_compiled *m = wasm_compile(wasm, n, WASM_F_INTERP, msg, sizeof msg);
    if (!m) {
        if (err) *err = mesh_wasm_fmt("jit: %s: %s", name, msg);
        free(path);
        return NULL;
    }
    *ms = ms_since(&t0);
    if (path) {
        char *e = NULL;
        if (!mesh_wasm_cache_store(path, wasm, n, &e)) mesh_error("wasm: cache %s", e ? e : path);
        free(e);
        char *dir = strdup(path);
        char *slash = dir ? strrchr(dir, '/') : NULL;
        if (slash) *slash = 0;
        if (dir) mesh_wasm_cache_evict(slash ? dir : ".", MESH_WASM_CACHE_CAP, evicted);
        free(dir);
    }
    free(path);
    *how = "compiled";
    return m;
}
