/* The ring tests of alpha-rust-mesh's mesh-abi pipe.rs, ported one for one:
 * shape_and_laps, fifo_full_and_wrap, mpmc_threads_lose_and_duplicate_nothing,
 * position_is_in_the_sum, reserve_push_is_bounded, skip_dead_waits_for_the_front. */
#include "mesh/abi/pipe_ring.h"
#include "mesh/check.h"
#include "mem.h"

#include <threads.h>

static void shape_and_laps(void) {
    CHECK_U(mesh_pipe_inbox_sums(4), 0x90);
    CHECK_U(mesh_pipe_inbox_data(4), 0xa0);
    CHECK_U(mesh_pipe_ring_bytes(256, 32), 0x880 + 256 * 32);
    CHECK_U(mesh_pipe_inbox_bytes(512, 32), 2 * (0x880 + 256 * 32));
    CHECK_U(mesh_pipe_ring_bytes(4, 8), 0xc0);
    mesh_pipe_inbox i = {0x100, 8, 8};
    mesh_pipe_ring s1 = mesh_pipe_inbox_side(&i, 1);
    CHECK_U(s1.base, 0x100 + 0xc0);
    CHECK_U(s1.cap, 4);
    mesh_pipe_inbox bad = {0, 2, 8};
    CHECK(mesh_pipe_inbox_valid(&i) && !mesh_pipe_inbox_valid(&bad));
    uint8_t a[8] = {1, 0, 0, 0, 2, 0, 0, 0}, b[8] = {2, 0, 0, 0, 1, 0, 0, 0};
    CHECK_U(mesh_pipe_rec_sum(a, 8), mesh_pipe_sum_part(0, 1) + mesh_pipe_sum_part(1, 2));
    CHECK(mesh_pipe_rec_sum(a, 8) != mesh_pipe_rec_sum(b, 8));
    mesh_pipe_ring r = {0, 4, 8}, r3 = {0, 3, 8}, r6 = {0, 4, 6};
    CHECK(mesh_pipe_ring_valid(&r) && !mesh_pipe_ring_valid(&r3) && !mesh_pipe_ring_valid(&r6));
    CHECK_U(mesh_pipe_ring_empty(&r, 0), 0);
    CHECK_U(mesh_pipe_ring_full(&r, 0), 1);
    CHECK_U(mesh_pipe_ring_empty(&r, 4), 2);
    CHECK_U(mesh_pipe_ring_full(&r, 7), 3);
    /* Positions wrap u32: the lap of p = 2^32 - 1 is followed by lap 0's words. */
    uint32_t last = UINT32_MAX;
    CHECK_U(mesh_pipe_ring_empty(&r, last + 1), (mesh_pipe_ring_full(&r, last) + 1) & mesh_pipe_ring_lap_mask(&r));
    CHECK_U(mesh_pipe_type_hash((const uint8_t *)"", 0), 0x811c9dc5u);
}

static void fifo_full_and_wrap(void) {
    mesh_pipe_ring r = {0x100, 4, 8};
    mem *m = mem_new(0x100 + mesh_pipe_ring_bytes(4, 8));
    const mesh_words *w = &m->words;
    /* Start near the u32 wrap to cross it. */
    uint32_t start = UINT32_MAX - 5;
    mem_set(m, 0x100 + MESH_PIPE_PI_HEAD, start);
    mem_set(m, 0x100 + MESH_PIPE_PI_TAIL, start);
    for (uint32_t i = 0; i < 4; i++) mem_set(m, mesh_pipe_ring_lap(&r, start + i), mesh_pipe_ring_empty(&r, start + i));
    uint32_t next = 0, got = 0, p, t;
    for (uint32_t round = 0; round < 10; round++) {
        while (mesh_pipe_ring_reserve_push(&r, w, 1, &p)) {
            uint32_t v[2] = {next, ~next};
            mem_put(m, &r, p, v, 2);
            mesh_pipe_ring_commit_push(&r, w, p);
            next++;
        }
        CHECK_U(mesh_pipe_ring_len(&r, w), 4);
        for (uint32_t k = 0; k < 1 + round % 4; k++) {
            CHECK(mesh_pipe_ring_reserve_pop(&r, w, &t));
            CHECK_U(mem_cell_word(m, &r, t, 0), got);
            CHECK_U(mem_cell_word(m, &r, t, 1), ~got);
            mesh_pipe_ring_commit_pop(&r, w, t);
            got++;
        }
    }
    while (mesh_pipe_ring_reserve_pop(&r, w, &t)) {
        CHECK_U(mem_cell_word(m, &r, t, 0), got);
        mesh_pipe_ring_commit_pop(&r, w, t);
        got++;
    }
    CHECK_U(got, next);
    CHECK(mem_get(m, 0x100 + MESH_PIPE_PI_FULL) >= 10);
    mem_free(m);
}

enum { PRODUCERS = 4, PER = 50000, CONSUMERS = 3 };
static const mesh_pipe_ring MPMC = {0, 64, 8};
static mem *mpmc_mem;
static _Atomic uint32_t seen[PRODUCERS * PER];
static _Atomic uint32_t done, torn;

static int producer(void *arg) {
    uint32_t k = (uint32_t)(uintptr_t)arg, p;
    for (uint32_t i = 0; i < PER; i++) {
        uint32_t v = k * PER + i;
        for (;;) {
            if (mesh_pipe_ring_reserve_push(&MPMC, &mpmc_mem->words, k, &p)) {
                uint32_t rec[2] = {v, v ^ 0xdeadbeefu};
                mem_put(mpmc_mem, &MPMC, p, rec, 2);
                mesh_pipe_ring_commit_push(&MPMC, &mpmc_mem->words, p);
                break;
            }
            thrd_yield();
        }
    }
    return 0;
}
static int consumer(void *arg) {
    (void)arg;
    uint32_t t;
    while (atomic_load_explicit(&done, memory_order_relaxed) < PRODUCERS * PER) {
        if (mesh_pipe_ring_reserve_pop(&MPMC, &mpmc_mem->words, &t)) {
            uint32_t x0 = mem_cell_word(mpmc_mem, &MPMC, t, 0), x1 = mem_cell_word(mpmc_mem, &MPMC, t, 1);
            mesh_pipe_ring_commit_pop(&MPMC, &mpmc_mem->words, t);
            if (x1 != (x0 ^ 0xdeadbeefu) || x0 >= PRODUCERS * PER) atomic_fetch_add(&torn, 1);
            else atomic_fetch_add_explicit(&seen[x0], 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&done, 1, memory_order_relaxed);
        } else {
            thrd_yield();
        }
    }
    return 0;
}
static void mpmc_threads_lose_and_duplicate_nothing(void) {
    mpmc_mem = mem_new(mesh_pipe_ring_bytes(64, 8));
    thrd_t th[PRODUCERS + CONSUMERS];
    for (uint32_t k = 0; k < PRODUCERS; k++) thrd_create(&th[k], producer, (void *)(uintptr_t)k);
    for (int k = 0; k < CONSUMERS; k++) thrd_create(&th[PRODUCERS + k], consumer, NULL);
    for (int k = 0; k < PRODUCERS + CONSUMERS; k++) thrd_join(th[k], NULL);
    CHECK_U(atomic_load(&torn), 0); /* "torn record" */
    uint32_t once = 0;
    for (uint32_t i = 0; i < PRODUCERS * PER; i++) once += atomic_load(&seen[i]) == 1;
    CHECK_U(once, PRODUCERS * PER);
    CHECK_U(mesh_pipe_ring_len(&MPMC, &mpmc_mem->words), 0);
    mem_free(mpmc_mem);
}

static void position_is_in_the_sum(void) {
    /* A cell and sum both left from the previous lap behind a fresh lap word fail the check (finding 5). */
    mesh_pipe_ring r = {0, 4, 8};
    mem *m = mem_new(mesh_pipe_ring_bytes(4, 8));
    const mesh_words *w = &m->words;
    uint8_t out[8];
    uint32_t p0, t, p;
    CHECK(mesh_pipe_pos_mix(0) != mesh_pipe_pos_mix(4));
    CHECK(mesh_pipe_ring_reserve_push(&r, w, 3, &p0));
    CHECK_U(mem_get(m, mesh_pipe_ring_sum(&r, p0)), mesh_pipe_owner_tag(3));
    uint32_t v78[2] = {7, 8};
    mem_put(m, &r, p0, v78, 2);
    mesh_pipe_ring_commit_push(&r, w, p0);
    CHECK(mesh_pipe_ring_take(&r, w, out, 8, &t));
    mesh_pipe_ring_commit_pop(&r, w, t);
    /* Producer of position 4 (same cell) advanced head and published its lap word, but the
     * consumer still sees the old cell and sum (a stale cache line): not taken. */
    for (int i = 1; i < 4; i++) {
        CHECK(mesh_pipe_ring_reserve_push(&r, w, 3, &p));
        uint32_t v[2] = {p, p};
        mem_put(m, &r, p, v, 2);
        mesh_pipe_ring_commit_push(&r, w, p);
        CHECK(mesh_pipe_ring_take(&r, w, out, 8, &t));
        mesh_pipe_ring_commit_pop(&r, w, t);
    }
    uint32_t p4;
    CHECK(mesh_pipe_ring_reserve_push(&r, w, 3, &p4));
    CHECK_U(p4, 4);
    const uint8_t old[8] = {7, 0, 0, 0, 8, 0, 0, 0};
    mem_set(m, mesh_pipe_ring_sum(&r, p4), mesh_pipe_rec_sum_at(old, 8, 0));
    mem_set(m, mesh_pipe_ring_cell(&r, p4), 7);
    mem_set(m, mesh_pipe_ring_cell(&r, p4) + 4, 8);
    mesh_pipe_ring_commit_push(&r, w, p4);
    CHECK(!mesh_pipe_ring_take(&r, w, out, 8, &t)); /* a stale record from position 0 passed as position 4 */
    mem_put(m, &r, p4, v78, 2);
    CHECK(mesh_pipe_ring_take(&r, w, out, 8, &t) && t == 4);
    mem_free(m);
}

static void collect(void *ctx, uint32_t p) {
    uint32_t *cells = ctx;
    cells[1 + cells[0]++] = p;
}
static void fail_cell(void *ctx, uint32_t p) {
    (void)ctx;
    CHECK(!"slot 1 committed everything");
    (void)p;
}
static void reserve_push_is_bounded(void) {
    /* A lap word that is neither empty(p) nor the previous lap's full: busy, at once (finding 2). */
    mesh_pipe_ring r = {0, 4, 8};
    mem *m = mem_new(mesh_pipe_ring_bytes(4, 8));
    uint32_t p, t;
    mem_set(m, mesh_pipe_ring_lap(&r, 0), 0xbb);
    CHECK(!mesh_pipe_ring_reserve_push(&r, &m->words, 1, &p));
    CHECK_U(mem_get(m, MESH_PIPE_PI_FULL), 0); /* not counted as backpressure */
    mem_free(m);
    /* A producer that reserved position 0 and died; the ring wrapped to its cell: busy, not a spin. */
    m = mem_new(mesh_pipe_ring_bytes(4, 8));
    const mesh_words *w = &m->words;
    uint32_t dead;
    CHECK(mesh_pipe_ring_reserve_push(&r, w, 9, &dead));
    for (int i = 1; i < 4; i++) {
        CHECK(mesh_pipe_ring_reserve_push(&r, w, 1, &p));
        uint32_t v[2] = {p, 0};
        mem_put(m, &r, p, v, 2);
        mesh_pipe_ring_commit_push(&r, w, p);
    }
    CHECK(!mesh_pipe_ring_reserve_push(&r, w, 1, &p));
    uint8_t out[8];
    CHECK(!mesh_pipe_ring_take(&r, w, out, 8, &t)); /* the dead cell blocks the consumer */
    /* meshd: the dead slot's cells, released at the front. */
    uint32_t cells[8] = {0};
    mesh_pipe_ring_abandoned(&r, w, 9, collect, cells);
    CHECK_U(cells[0], 1);
    CHECK_U(cells[1], dead);
    mesh_pipe_ring_abandoned(&r, w, 1, fail_cell, NULL);
    CHECK_I(mesh_pipe_ring_skip_dead(&r, w, 9, dead), 1);
    CHECK_I(mesh_pipe_ring_skip_dead(&r, w, 9, dead), -1);
    for (uint32_t want = 1; want < 4; want++) {
        CHECK(mesh_pipe_ring_take(&r, w, out, 8, &t));
        CHECK_U(t, want);
        mesh_pipe_ring_commit_pop(&r, w, t);
    }
    /* The dead cell is usable again: the ring is whole. */
    for (int i = 0; i < 4; i++) {
        CHECK(mesh_pipe_ring_reserve_push(&r, w, 1, &p));
        uint32_t v[2] = {p, 0};
        mem_put(m, &r, p, v, 2);
        mesh_pipe_ring_commit_push(&r, w, p);
    }
    CHECK_U(mesh_pipe_ring_len(&r, w), 4);
    mem_free(m);
}

static void skip_dead_waits_for_the_front(void) {
    mesh_pipe_ring r = {0, 8, 8};
    mem *m = mem_new(mesh_pipe_ring_bytes(8, 8));
    const mesh_words *w = &m->words;
    uint32_t a, dead, t;
    CHECK(mesh_pipe_ring_reserve_push(&r, w, 1, &a));
    uint32_t v[2] = {1, 1};
    mem_put(m, &r, a, v, 2);
    mesh_pipe_ring_commit_push(&r, w, a);
    CHECK(mesh_pipe_ring_reserve_push(&r, w, 2, &dead));
    CHECK_I(mesh_pipe_ring_skip_dead(&r, w, 2, dead), 0); /* position 0 is still queued */
    uint8_t out[8];
    CHECK(mesh_pipe_ring_take(&r, w, out, 8, &t));
    mesh_pipe_ring_commit_pop(&r, w, t);
    CHECK_I(mesh_pipe_ring_skip_dead(&r, w, 2, dead), 1);
    CHECK_U(mesh_pipe_ring_len(&r, w), 0);
    mem_free(m);
}

int main(void) {
    shape_and_laps();
    fifo_full_and_wrap();
    mpmc_threads_lose_and_duplicate_nothing();
    position_is_in_the_sum();
    reserve_push_is_bounded();
    skip_dead_waits_for_the_front();
    return mesh_check_done();
}
