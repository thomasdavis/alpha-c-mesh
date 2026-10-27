/* train-wasm in a lockstep group of 2 (rank 0), the test playing member 1 through the exchange
 * page (kcheck 5a-5c: "the CPU playing the other members"): rounds that find member 1 there give
 * the CPU reference's params bitwise (in Z, member 1's zero sums folded in); a round member 1 never
 * reaches times out after the bounded wait (PEER_POLLS x PEER_POLL_NS) with T_ERR, T_PENDING and
 * the timeouts counter -- and without FAULT_TIME, though the wait is longer than task_ms: the wait
 * re-arms the budget; the next task resumes the pending round without recomputing it. */
#include "mesh/abi/control_block.h"
#include "mesh/abi/exchange.h"
#include "mesh/abi/header.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/view_train.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"
#include "mesh/ftz.h"
#include "mesh/ref/cubic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TRAIN = "name = \"train\"\nabi = 6\nview_bytes = 0x1000\nnodes = 1\npolicy = \"continuous\"\nfanout = 1\n"
                           "peer = \"lockstep\"\nbackends = [\"wasm\"]\ntask_ms = 80\n";

static mesh_kit kit;
static uint32_t vu(uint32_t off) { return mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, 0, off)); }
static float vf(uint32_t off) { return mesh_ftz_from_bits(vu(off)); }

static float xs[MESH_TRAIN_SAMPLES], ys[MESH_TRAIN_SAMPLES], p[4];

/* The reference for steps [s, s + n): rank 0 of 2 takes offset 2 t, and adds member 1's zeros. */
static void reference(uint32_t s, uint32_t n, float lr, float inv) {
    for (uint32_t t = s; t < s + n; t++) {
        float sums[5];
        mesh_cubic_step_sums(p, xs, ys, t * 2, sums);
        for (int j = 0; j < 5; j++) sums[j] = mesh_ftz_add(sums[j], 0.0f);
        mesh_cubic_apply(p, sums, lr, inv);
    }
}

static bool same(void) {
    for (uint32_t k = 0; k < 4; k++)
        if (mesh_ftz_bits(vf(MESH_TRAIN_T_PARAMS + 4 * k)) != mesh_ftz_bits(p[k])) return false;
    return true;
}

int main(void) {
    char *wasm = mesh_kit_artifact("ajax-mesh-prog-train-wasm");
    CHECK(wasm != NULL);
    if (!wasm) return mesh_check_done();
    char *err = NULL;
    CHECK(mesh_kit_open(&kit, NULL, &err));
    mesh_mailbox_set(kit.mb, MESH_H_GROUP_RANK, 0);
    mesh_mailbox_set(kit.mb, MESH_H_GROUP_N, 2);
    /* Member 1 is always ahead (its slots stay zero). */
    mesh_mailbox_publish64(kit.mb, mesh_ex_counter(1), 1ull << 40);
    if (!mesh_kit_load(&kit, 0, TRAIN, wasm, &err)) {
        fprintf(stderr, "load: %s\n", err);
        CHECK(false);
        return mesh_check_done();
    }
    uint32_t r, fault;
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_INIT, 0, &r, &fault, 60000));
    float lr = vf(MESH_TRAIN_T_LR), inv = vf(MESH_TRAIN_T_INV_BATCH);
    CHECK_U(mesh_ftz_bits(inv), mesh_ftz_bits(1.0f / 64.0f));
    mesh_mailbox_set(kit.mb, mesh_kit_view(&kit, 0, MESH_TRAIN_T_STEPS_PER_TASK), 16);
    mesh_cubic_samples(xs, ys);
    memset(p, 0, sizeof p);
    uint32_t s = 0;
    for (int task = 0; task < 4; task++) {
        CHECK(mesh_kit_task(&kit, 0, MESH_OP_TASK, s, &r, &fault, 60000));
        CHECK_U(r, 16);
        reference(s, 16, lr, inv);
        CHECK(same());
        s += 16;
    }
    CHECK_U(vu(MESH_TRAIN_T_NEXT_STEP), 64);
    CHECK_U(vu(MESH_TRAIN_T_PENDING), 0);
    CHECK_U(mesh_mailbox_get64(kit.mb, mesh_ex_counter(0)), 64);
    /* Member 1 falls behind: the round of step 64 (n = 65) times out. */
    mesh_mailbox_publish64(kit.mb, mesh_ex_counter(1), 64);
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_TASK, s, &r, &fault, 60000));
    CHECK_U(r, 0);
    CHECK_U(fault, 0);
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_prog_ctl(0) + MESH_P_FAULT), 0);
    CHECK_U(vu(MESH_TRAIN_T_ERR), 1);
    CHECK_U(vu(MESH_TRAIN_T_PENDING), 1);
    CHECK_U(vu(MESH_TRAIN_T_ROUND), s + 1);
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_prog_ctl(0) + MESH_P_COUNTERS + 4 * MESH_TRAIN_C_TIMEOUTS), 1);
    CHECK(same()); /* nothing applied */
    /* Member 1 catches up: the pending round resumes (its published slot), the task completes. */
    mesh_mailbox_publish64(kit.mb, mesh_ex_counter(1), 1ull << 40);
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_TASK, s, &r, &fault, 60000));
    CHECK_U(r, 16);
    reference(s, 16, lr, inv);
    CHECK(same());
    CHECK_U(vu(MESH_TRAIN_T_ERR), 0);
    CHECK_U(vu(MESH_TRAIN_T_PENDING), 0);
    mesh_kit_close(&kit);
    free(wasm);
    free(err);
    return mesh_check_done();
}
