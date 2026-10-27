/* The native lifecycle fixture (mesh-worker src/bin/test_worker.rs; mesh-native's
 * tests, mesh-lifecheck --native): op 0: arg * 3 + 7 (+ 1000 x param, so two
 * images tell apart); OP_INIT: the view magic; 3: sleep arg ms; 4: param; 5:
 * the pid; 9: panic; 10: allocate and touch arg MiB (a memory cap kills it);
 * 11: read past the view (a fault); 12: spin arg ms (CPU); 13: SIGTERM itself
 * if arg == 0, else exit(arg), mid-task (no core dump).  Its setup sleeps
 * MESH_TEST_SETUP_MS (a slow model load). */
#include "mesh/worker.h"

#include "mesh/abi/ops.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAGIC 0x57544e4eu /* "NNTW" */

static void sleep_ms(uint64_t ms) {
    struct timespec t = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

static void init(const mesh_worker_ctx *ctx, void *u) {
    (void)u;
    mesh_ctx_set_u32(ctx, 0, MAGIC);
    mesh_ctx_set_u32(ctx, 4, 0);
}

static uint32_t task(const mesh_worker_ctx *ctx, uint32_t op, uint32_t arg, void *u) {
    (void)u;
    switch (op) {
    case MESH_OP_TASK:
        mesh_ctx_count(ctx, 0, 1);
        mesh_ctx_set_u32(ctx, 4, arg);
        return arg * 3 + 7 + 1000 * mesh_ctx_param(ctx);
    case 3:
        sleep_ms(arg);
        return arg;
    case 4: return mesh_ctx_param(ctx);
    case 5: return (uint32_t)getpid();
    case 9: mesh_worker_panic("deliberate crash (op 9, arg %u)", (unsigned)arg);
    case 10: {
        size_t n = (size_t)arg << 20;
        volatile uint8_t *v = calloc(n ? n : 1, 1);
        if (!v) mesh_worker_panic("out of memory (%u MiB)", (unsigned)arg);
        for (size_t i = 0; i < n; i += 4096) v[i] = 1;
        free((void *)v);
        return (uint32_t)(n >> 20);
    }
    case 11: return mesh_ctx_view_u32(ctx, mesh_ctx_view_len(ctx));
    case 12: {
        uint64_t t0 = mesh_worker_now_ns();
        volatile uint64_t x = 0;
        while (mesh_worker_now_ns() - t0 < (uint64_t)arg * 1000000u) x = x * 6364136223846793005u + 1;
        return (uint32_t)x;
    }
    case 13:
        if (arg == 0) raise(SIGTERM);
        exit((int)arg);
    default: return MESH_R_BAD_OP;
    }
}

int main(void) {
    const char *ms = getenv("MESH_TEST_SETUP_MS");
    if (ms) sleep_ms(strtoull(ms, NULL, 10));
    printf("test worker %d set up\n", (int)getpid());
    fflush(stdout);
    mesh_worker_run(&(mesh_worker_program){init, task, NULL});
}
