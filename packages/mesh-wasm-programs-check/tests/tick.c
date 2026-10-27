/* tick-wasm: init writes the pattern, each task ticks one word, OP_VERIFY finds no mismatch, and
 * a word changed behind its back is found. */
#include "mesh/abi/control_block.h"
#include "mesh/abi/ops.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"

#include <stdio.h>
#include <stdlib.h>

static const char *TICK = "name = \"tick\"\nabi = 6\nview_bytes = 0x10000\nview_abi = 1\nnodes = 1\n"
                          "policy = \"continuous\"\nbackends = [\"wasm\"]\ntask_ms = 10000\n";

int main(void) {
    char *wasm = mesh_kit_artifact("ajax-mesh-prog-tick-wasm");
    CHECK(wasm != NULL);
    if (!wasm) return mesh_check_done();
    mesh_kit kit;
    char *err = NULL;
    CHECK(mesh_kit_open(&kit, NULL, &err));
    if (!mesh_kit_load(&kit, 0, TICK, wasm, &err)) {
        fprintf(stderr, "load: %s\n", err);
        CHECK(false);
        return mesh_check_done();
    }
    uint32_t r, fault;
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_INIT, 0, &r, &fault, 10000));
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, 0, 0)), 0x4b434954u);
    CHECK(mesh_kit_tasks(&kit, 0, MESH_OP_TASK, 0, 20000, &fault, 20000));
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, 0, 4)), 20000);
    CHECK(mesh_kit_task(&kit, 0, 3, 0, &r, &fault, 10000));
    CHECK_U(r, 0);
    /* Corrupt a recently ticked word: verify sees it. */
    size_t w = mesh_kit_view(&kit, 0, 0x40 + 4 * ((20000 - 1) % ((0x10000 - 0x40) / 4)));
    mesh_mailbox_set(kit.mb, w, mesh_mailbox_get(kit.mb, w) ^ 1);
    CHECK(mesh_kit_task(&kit, 0, 3, 0, &r, &fault, 10000));
    CHECK(r >= 1);
    CHECK(mesh_kit_task(&kit, 0, 99, 0, &r, &fault, 10000));
    CHECK_U(r, MESH_R_BAD_OP);
    mesh_kit_close(&kit);
    free(wasm);
    free(err);
    return mesh_check_done();
}
