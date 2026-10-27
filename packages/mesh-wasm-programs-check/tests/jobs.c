/* hashjob-wasm: OP_JOB items against the CPU reference (mesh-ref-sha256d): the fold of K
 * double-SHA-256s and the job_out record; the fail and trap test items. */
#include "mesh/abi/control_block.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/ring.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"
#include "mesh/ref/sha256d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *HASHJOB = "name = \"hashjob\"\nabi = 6\nbackends = [\"wasm\"]\nview_bytes = 0x1000\nnodes = 4\n"
                             "policy = \"job\"\nfanout = 4\nparam = 16\njob_base = 0x08\njob_out = \"0x100:16\"\n"
                             "task_ms = 20000\n";

static uint32_t fold(uint64_t i, uint64_t k) {
    uint32_t f = 0;
    for (uint64_t j = 0; j < k; j++) {
        uint8_t b[8], h[32];
        uint64_t x = i * k + j;
        for (int n = 0; n < 8; n++) b[n] = (uint8_t)(x >> (8 * n));
        mesh_sha256d(b, 8, h);
        f ^= (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
    }
    return f == MESH_R_JOB_FAIL ? f - 1 : f;
}

int main(void) {
    char *wasm = mesh_kit_artifact("ajax-mesh-prog-hashjob-wasm");
    CHECK(wasm != NULL);
    if (!wasm) return mesh_check_done();
    mesh_kit kit;
    char *err = NULL;
    CHECK(mesh_kit_open(&kit, NULL, &err));
    if (!mesh_kit_load(&kit, 0, HASHJOB, wasm, &err)) {
        fprintf(stderr, "load: %s\n", err);
        CHECK(false);
        return mesh_check_done();
    }
    uint32_t r, fault;
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_INIT, 0, &r, &fault, 20000));
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, 0, 0)), 0x4a485348u);
    mesh_mailbox_set64(kit.mb, mesh_kit_view(&kit, 0, 0x08), 1000);
    for (uint32_t a = 0; a < 12; a++) {
        uint32_t t = kit.slot[0].next;
        CHECK(mesh_kit_task(&kit, 0, MESH_OP_JOB, a, &r, &fault, 20000));
        CHECK_U(r, fold(1000 + a, 16));
        uint8_t b[8], h[32], got[16];
        for (int n = 0; n < 8; n++) b[n] = (uint8_t)((1000 + (uint64_t)a) >> (8 * n));
        mesh_sha256d(b, 8, h);
        for (int n = 0; n < 16; n++) got[n] = mesh_mailbox_get8(kit.mb, mesh_kit_view(&kit, 0, 0x100 + 16 * (t % MESH_PROG_RESULTS) + (size_t)n));
        CHECK(memcmp(got, h, 16) == 0);
    }
    CHECK_U(mesh_mailbox_get64(kit.mb, mesh_kit_view(&kit, 0, 0x20)), 12);
    CHECK_U(mesh_mailbox_get64(kit.mb, mesh_kit_view(&kit, 0, 0x28)), 12 * 16);
    CHECK_U(mesh_mailbox_get64(kit.mb, mesh_kit_view(&kit, 0, 0x30)), 1011);
    /* Item 1003 fails (R_JOB_FAIL); then clear; then item 1004 traps. */
    CHECK(mesh_kit_task(&kit, 0, 3, 1003, &r, &fault, 20000));
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_JOB, 3, &r, &fault, 20000));
    CHECK_U(r, MESH_R_JOB_FAIL);
    CHECK(mesh_kit_task(&kit, 0, 5, 0, &r, &fault, 20000));
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_JOB, 3, &r, &fault, 20000));
    CHECK_U(r, fold(1003, 16));
    CHECK(mesh_kit_task(&kit, 0, 4, 1004, &r, &fault, 20000));
    CHECK(!mesh_kit_task(&kit, 0, MESH_OP_JOB, 4, &r, &fault, 20000));
    CHECK_U(fault, MESH_FAULT_TRAP | 10u << 8);
    mesh_kit_close(&kit);
    free(wasm);
    free(err);
    return mesh_check_done();
}
