/* load.rs's program_sampler_reads_records_and_trace. */
#include "mesh/check.h"
#include "mesh/load_prog.h"

#include <stdlib.h>
#include <string.h>

static float F(const uint8_t *r, size_t o) { float f; memcpy(&f, r + o, 4); return f; }
static uint32_t U(const uint8_t *r, size_t o) { uint32_t u; memcpy(&u, r + o, 4); return u; }
static bool zero_after(const uint8_t *r, size_t from) { for (size_t i = from; i < MESH_PLOAD_BYTES; i++) if (r[i]) return false; return true; }

int main(void) {
    mesh_mailbox mb = mesh_mailbox_test();
    uint32_t s = 3;
    mesh_mailbox_set(mb, mesh_prog_entry(s) + MESH_PT_STATE, MESH_ST_RUNNING);
    mesh_mailbox_set(mb, mesh_prog_entry(s) + MESH_PT_NODES, 2);
    mesh_mailbox_set64(mb, mesh_prog_entry(s) + MESH_PT_PTX_HASH, 0xabc);
    static mesh_prog_sampler p;
    uint8_t rec[MESH_PLOAD_BYTES];
    mesh_nl d;
    uint32_t mg;
    mesh_prog_sample(&p, mb, s, 1.0, 0, rec, &d, &mg);
    CHECK_U(rec[MESH_PL_FLAGS], MESH_PLF_LOADED);
    CHECK(zero_after(rec, 4));
    size_t r0 = mesh_node_load(s, 0);
    mesh_mailbox_set64(mb, r0 + MESH_NL_OBS_NS, 1000000000ull);
    mesh_mailbox_set64(mb, r0 + MESH_NL_LOST_NS, 500000000ull);
    mesh_mailbox_set(mb, r0 + MESH_NL_GAPS, 400);
    mesh_mailbox_set(mb, r0 + MESH_NL_MAX_GAP_NS, 2500000);
    mesh_mailbox_set(mb, r0 + MESH_NL_WIN, 0);
    mesh_mailbox_set64(mb, mesh_node_rec(s, 1) + MESH_N_BUSY_NS, 20000000);
    mesh_mailbox_set(mb, mesh_node_rec(s, 1) + MESH_N_TASKS, 10);
    mesh_mailbox_set(mb, mesh_prog_ctl(s) + MESH_P_DONE, 10);
    for (uint32_t i = 0; i < 10; i++) {
        size_t r = mesh_trace_rec(s, i);
        mesh_mailbox_set(mb, r, s | 1u << 8);
        mesh_mailbox_set(mb, r + 8, 1000 * i);
        mesh_mailbox_set(mb, r + 12, 1000 * i + 1000000 + 100000 * i);
    }
    size_t r = mesh_trace_rec(s, 10);
    mesh_mailbox_set(mb, r, s | MESH_OP_INIT << 16);
    mesh_mailbox_set(mb, r + 12, 50000000);
    mesh_mailbox_set(mb, mesh_prog_ctl(s) + MESH_P_TRACE_N, 11);
    mesh_prog_sample(&p, mb, s, 1.0, 0, rec, &d, &mg);
    CHECK_U(U(rec, MESH_PL_FLAGS), MESH_PLF_LOADED | MESH_PLF_GPU | MESH_PLF_LAT | MESH_PLF_TASK);
    CHECK(F(rec, MESH_PL_SHARE) == 0.5f && F(rec, MESH_PL_SHARE10) == 0.5f);
    CHECK(F(rec, MESH_PL_MAX_GAP_US) == 2500.0f && mg == 2500000);
    CHECK_U(d.gaps, 400);
    CHECK_U(U(rec, MESH_PL_LAT_N), 10);
    CHECK(F(rec, MESH_PL_P50_US) == 1500.0f && F(rec, MESH_PL_MAX_US) == 1900.0f);
    CHECK(F(rec, MESH_PL_TASK_US) == 2000.0f && F(rec, MESH_PL_TASKS_S) == 10.0f);
    CHECK(F(rec, MESH_PL_BUSY) > 0.00999f && F(rec, MESH_PL_BUSY) < 0.01001f);
    mesh_prog_sample(&p, mb, s, 1.0, 1, rec, &d, &mg);
    CHECK(F(rec, MESH_PL_MAX_GAP_US) == 0.0f);
    CHECK(F(rec, MESH_PL_SHARE) == 0.5f);
    mesh_mailbox_set(mb, mesh_prog_entry(s) + MESH_PT_LOADS, 2);
    mesh_prog_sample(&p, mb, s, 1.0, 2, rec, &d, &mg);
    CHECK(zero_after(rec, 4));
    mesh_prog_sampler_reset(&p);
    free(mb.base);
    return mesh_check_done();
}
