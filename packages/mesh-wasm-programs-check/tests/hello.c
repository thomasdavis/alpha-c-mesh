/* hello-wasm in the wasm executor: arg * 3 + 7, init, counters, an unknown op, and the executor's
 * test hooks: view loads, bulk reads, a trap (FAULT_TRAP with wasmtime's code), a spin cut off by
 * task_ms (FAULT_TIME), memory growth up to mem_mb. */
#include "mesh/abi/control_block.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/ops.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"

#include <stdio.h>
#include <stdlib.h>

static const char *MANIFEST = "name       = \"hello\"\n"
                              "abi        = 6\n"
                              "view_bytes = 0x1000\n"
                              "view_abi   = 1\n"
                              "snap_bytes = 0x40\n"
                              "nodes      = 2\n"
                              "policy     = \"on_demand\"\n"
                              "counters   = [\"tasks\", \"inits\", \"\", \"\"]\n"
                              "fields     = [\"magic:u32:0x0\", \"last_arg:u32:0x4\", \"last_result:u32:0x8\"]\n"
                              "autostart  = true\n"
                              "backends   = [\"cuda\", \"metal\", \"wasm\"]\n";

static const char *TIGHT = "name = \"hello\"\nabi = 6\nview_bytes = 0x1000\nnodes = 1\npolicy = \"on_demand\"\n"
                           "backends = [\"wasm\"]\nmem_mb = 1\ntask_ms = 20\n";

int main(void) {
    char *wasm = mesh_kit_artifact("ajax-mesh-prog-hello-wasm");
    CHECK(wasm != NULL);
    if (!wasm) return mesh_check_done();
    mesh_kit kit;
    char *err = NULL;
    CHECK(mesh_kit_open(&kit, NULL, &err));
    if (!mesh_kit_load(&kit, 0, MANIFEST, wasm, &err)) {
        fprintf(stderr, "load: %s\n", err);
        CHECK(false);
        return mesh_check_done();
    }
    uint32_t r, fault;
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_INIT, 0, &r, &fault, 5000));
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, 0, 0)), 0x4f4c4548u);
    for (uint32_t a = 0; a < 100; a++) {
        CHECK(mesh_kit_task(&kit, 0, MESH_OP_TASK, a * 1000003u, &r, &fault, 5000));
        CHECK_U(r, a * 1000003u * 3u + 7u);
    }
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, 0, 4)), 99u * 1000003u);
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, 0, 8)), 99u * 1000003u * 3u + 7u);
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_prog_ctl(0) + MESH_P_COUNTERS), 100);
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_prog_ctl(0) + MESH_P_COUNTERS + 4), 1);
    CHECK(mesh_kit_task(&kit, 0, 99, 0, &r, &fault, 5000));
    CHECK_U(r, MESH_R_BAD_OP);
    /* Op 8: loads of the view (the magic and zeros); op 9: bulk reads. */
    CHECK(mesh_kit_task(&kit, 0, 8, 1024, &r, &fault, 5000));
    CHECK_U(r, 0x4f4c4548u + 99u * 1000003u + (99u * 1000003u * 3u + 7u));
    CHECK(mesh_kit_task(&kit, 0, 9, 10, &r, &fault, 5000));
    CHECK_U(r, 0); /* 10 reads of the first byte 0x48 xor'd: an even count */
    /* Op 10 traps: FAULT_TRAP | (unreachable = 9) + 1 << 8. */
    CHECK(!mesh_kit_task(&kit, 0, 10, 0, &r, &fault, 5000));
    CHECK_U(fault, MESH_FAULT_TRAP | 10u << 8);
    mesh_kit_unload(&kit, 0);
    /* A tight manifest: op 11 spins past task_ms (FAULT_TIME); op 12 grows past mem_mb (-1). */
    CHECK(mesh_kit_load(&kit, 1, TIGHT, wasm, &err));
    CHECK(mesh_kit_task(&kit, 1, 12, 1, &r, &fault, 5000));
    CHECK_U(r, 0xffffffffu);
    CHECK(!mesh_kit_task(&kit, 1, 11, 0, &r, &fault, 5000));
    CHECK_U(fault, MESH_FAULT_TIME);
    mesh_kit_unload(&kit, 1);
    mesh_kit_close(&kit);
    free(wasm);
    free(err);
    return mesh_check_done();
}
