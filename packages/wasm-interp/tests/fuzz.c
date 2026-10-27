/* Hostile modules: random mutations of real modules, and random bytes. Every one must be refused or
 * run to a result, a trap or an interrupt; none may crash or touch memory it shouldn't (run this
 * under --profile asan). Deterministic: a fixed seed. */
#define _XOPEN_SOURCE 700
#include "mesh/check.h"
#include "wasm/builder.h"
#include "wasm/interp.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fixtures/prog.h"

static _Atomic uint64_t epoch;
static _Atomic int stop;

static void *ticker(void *arg) {
    (void)arg;
    struct timespec ms = {0, 1000000};
    while (!atomic_load(&stop)) {
        atomic_fetch_add(&epoch, 1);
        nanosleep(&ms, NULL);
    }
    return NULL;
}

static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint32_t next(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)rng;
}

static int host_any(wasm_instance *I, void *ctx, uint64_t *io) {
    (void)I, (void)ctx;
    io[0] = 1;
    return WASM_NUM_OK;
}

static long compiled, instantiated, calls, traps, by_trap[16];

static void try_module(const uint8_t *b, size_t n) {
    char err[256];
    wasm_compiled *c = wasm_compile(b, n, WASM_F_INTERP, err, sizeof err);
    if (!c) return;
    compiled++;
    /* Whatever it imports, offer a function of that name and signature. */
    const wasm_module *m = wasm_compiled_module(c);
    wasm_host_func hosts[8];
    char sigs[8][40];
    size_t nh = 0;
    for (uint32_t i = 0; i < m->nimports && nh < 8; i++) {
        const wasm_import *im = &m->imports[i];
        if (im->kind != WASM_EXTERN_FUNC) continue;
        const wasm_functype *t = &m->types[im->func_type];
        if (t->nparams + t->nresults + 2 > sizeof sigs[0]) continue;
        char *s = sigs[nh];
        size_t k = 0;
        for (uint32_t p = 0; p < t->nparams; p++) s[k++] = t->params[p] == WASM_I64 ? 'l' : t->params[p] == WASM_F32 ? 'f' : t->params[p] == WASM_F64 ? 'd' : 'i';
        s[k++] = '>';
        for (uint32_t p = 0; p < t->nresults; p++) s[k++] = t->results[p] == WASM_I64 ? 'l' : t->results[p] == WASM_F32 ? 'f' : t->results[p] == WASM_F64 ? 'd' : 'i';
        s[k] = 0;
        static char names[8][64], mods[8][64];
        snprintf(mods[nh], sizeof mods[nh], "%.*s", (int)im->module_len, im->module);
        snprintf(names[nh], sizeof names[nh], "%.*s", (int)im->name_len, im->name);
        hosts[nh] = (wasm_host_func){mods[nh], names[nh], s, host_any, NULL};
        nh++;
    }
    wasm_store_limits lim = {16u << 20, 4, 1, 1u << 14, 1000};
    wasm_instance *I = wasm_instantiate(c, hosts, nh, &lim, NULL, err, sizeof err);
    if (I) {
        instantiated++;
        for (uint32_t f = m->nfunc_imports; f < m->nfuncs && f < m->nfunc_imports + 16; f++) {
            const wasm_functype *t = wasm_func_type(m, f);
            uint64_t *io = calloc(t->nparams + t->nresults + 1, sizeof *io);
            wasm_set_epoch(I, &epoch);
            wasm_set_deadline(I, atomic_load(&epoch) + 3);
            int r = wasm_call(I, f, io);
            calls++;
            free(io);
            if (r != WASM_NUM_OK) {
                traps++;
                if (r >= 0 && r < 16) by_trap[r]++;
                break; /* a trapped instance is not called again (as meshd) */
            }
        }
        wasm_instance_free(I);
    }
    wasm_compiled_free(c);
}

/* A module that uses most instructions: the builder's, with a loop, memory, a table, calls. */
static void seed_module(wasm_buf *out) {
    wasm_builder w = {0};
    uint32_t t = wasm_builder_type(&w, "ii>i");
    wasm_builder_import_func(&w, "mesh", "info", wasm_builder_type(&w, "i>i"));
    uint32_t f;
    wasm_buf *b = wasm_builder_func_add(&w, t, "il", &f);
    static const uint8_t body[] = {0x03, 0x40, 0x20, 2, 0x41, 1, 0x6a, 0x22, 2, 0x20, 0, 0x28, 2, 4, 0x20, 1, 0x36, 2, 8,
                                   0x20, 1, 0x10, 0, 0x1a, 0x20, 2, 0x41, 10, 0x49, 0x0d, 0, 0x0b, 0x20, 0, 0x20, 1, 0x11,
                                   0, 0, 0x42, 5, 0x21, 3, 0x20, 3, 0xa7, 0x6a};
    wb_bytes(b, body, sizeof body);
    wasm_builder_memory(&w, 1, 4, true);
    wasm_builder_table(&w, 2, 2, true);
    uint32_t el[] = {1, 1};
    wasm_builder_elem(&w, 0, el, 2);
    wasm_builder_data(&w, 0, "abcdefgh", 8);
    wasm_builder_add_export(&w, "f", WASM_EXTERN_FUNC, f);
    wasm_builder_finish(&w, out);
    wasm_builder_free(&w);
}

int main(void) {
    pthread_t th;
    CHECK(pthread_create(&th, NULL, ticker, NULL) == 0);
    wasm_buf seed;
    seed_module(&seed);
    const struct {
        const uint8_t *b;
        size_t n;
    } seeds[] = {{seed.data, seed.len}, {prog_wasm, prog_wasm_len}};
    uint8_t *buf = malloc(prog_wasm_len + seed.len + 64);
    for (int round = 0; round < 6000; round++) {
        const uint8_t *src = seeds[round & 1].b;
        size_t n = seeds[round & 1].n;
        memcpy(buf, src, n);
        int edits = 1 + (int)(next() % 4);
        for (int e = 0; e < edits; e++) {
            uint32_t at = next() % (uint32_t)n;
            switch (next() % 4) {
            case 0: buf[at] ^= (uint8_t)(1u << (next() % 8)); break;
            case 1: buf[at] = (uint8_t)next(); break;
            case 2: buf[at] = (uint8_t)(next() % 2 ? 0xff : 0x00); break;
            case 3: if (n > 9) n--; break; /* truncate */
            }
        }
        try_module(buf, n);
    }
    for (int round = 0; round < 3000; round++) {
        size_t n = 8 + next() % 64;
        memcpy(buf, "\0asm\x01\0\0\0", 8);
        for (size_t i = 8; i < n; i++) buf[i] = (uint8_t)next();
        try_module(buf, n);
    }
    free(buf);
    wasm_buf_free(&seed);
    atomic_store(&stop, 1);
    pthread_join(th, NULL);
    /* The mutations must have reached every stage, not just the decoder. */
    CHECK(compiled > 100);
    CHECK(instantiated > 50);
    CHECK(calls > 50);
    fprintf(stderr, "fuzz: %ld compiled, %ld instantiated, %ld calls, %ld traps (", compiled, instantiated, calls, traps);
    for (int k = 0; k < 16; k++)
        if (by_trap[k]) fprintf(stderr, " %d:%ld", k, by_trap[k]);
    fprintf(stderr, " )\n");
    return mesh_check_done();
}
