/* pipesrc-wasm -> pipesink-w-wasm over a pipe (mesh-abi pipe): pipesrc in direct mode hashes its
 * msgs and pushes digests into pipesink-w's inbox; pipesink-w drains them and checks each against
 * the CPU's sha256d of the msg rebuilt from its seq. Every digest arrives, none is bad. */
#include "mesh/abi/ops.h"
#include "mesh/abi/view_pipedemo.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"

#include <stdio.h>
#include <stdlib.h>

static const char *SRC = "name = \"pipesrc\"\nabi = 6\nview_bytes = 0x100\nnodes = 1\npolicy = \"continuous\"\n"
                         "float = \"ieee\"\nbackends = [\"wasm\"]\nfeeds = [\"pipesink-w:digest\"]\ntask_ms = 60000\n";
static const char *SINK = "name = \"pipesink-w\"\nabi = 6\nbackends = [\"wasm\"]\nfloat = \"ieee\"\nview_bytes = 0x3d000\n"
                          "inbox = \"0x4100:4096:48\"\ninbox_type = \"digest\"\nnodes = 1\npolicy = \"continuous\"\n"
                          "task_ms = 60000\n";

int main(void) {
    char *src = mesh_kit_artifact("ajax-mesh-prog-pipesrc-wasm"), *sink = mesh_kit_artifact("ajax-mesh-prog-pipesink-w-wasm");
    CHECK(src && sink);
    if (!src || !sink) return mesh_check_done();
    mesh_kit kit;
    char *err = NULL;
    CHECK(mesh_kit_open(&kit, NULL, &err));
    if (!mesh_kit_load(&kit, 1, SINK, sink, &err) || !mesh_kit_load(&kit, 0, SRC, src, &err)) {
        fprintf(stderr, "load: %s\n", err);
        CHECK(false);
        return mesh_check_done();
    }
    mesh_kit_connect(&kit, 0, 1);
    uint32_t r, fault;
    CHECK(mesh_kit_task(&kit, 1, MESH_OP_INIT, 0, &r, &fault, 60000));
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_INIT, 0, &r, &fault, 60000));
    CHECK(mesh_kit_task(&kit, 0, MESH_PIPEDEMO_OP_MODE, MESH_PIPEDEMO_MODE_DIRECT, &r, &fault, 60000));
    uint32_t sent = 0, got = 0;
    for (int round = 0; round < 8; round++) {
        CHECK(mesh_kit_task(&kit, 0, MESH_OP_TASK, 0, &r, &fault, 60000));
        sent += r;
        CHECK(mesh_kit_task(&kit, 1, MESH_OP_TASK, 0, &r, &fault, 60000));
        got += r;
    }
    CHECK_U(sent, 8 * MESH_PIPEDEMO_SRC_BATCH);
    CHECK_U(got, sent);
    size_t v = mesh_kit_view(&kit, 1, 0);
    CHECK_U(mesh_mailbox_get(kit.mb, v + MESH_PIPEDEMO_K_RECORDS), sent);
    CHECK_U(mesh_mailbox_get(kit.mb, v + MESH_PIPEDEMO_K_BAD), 0);
    CHECK_U(mesh_mailbox_get(kit.mb, v + MESH_PIPEDEMO_K_CPU), sent);
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, 0, MESH_PIPEDEMO_S_SENT)), sent);
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, 0, MESH_PIPEDEMO_S_DST)), 2); /* slot 1 + 1 */
    /* An empty inbox: the sink waits a bounded time and counts it. */
    CHECK(mesh_kit_task(&kit, 1, MESH_OP_TASK, 0, &r, &fault, 60000));
    CHECK_U(r, 0);
    CHECK_U(mesh_mailbox_get(kit.mb, v + MESH_PIPEDEMO_K_EMPTY), 1);
    /* Stats: the latency quantiles. */
    CHECK(mesh_kit_task(&kit, 1, MESH_PIPEDEMO_OP_STATS, 0, &r, &fault, 60000));
    CHECK(mesh_mailbox_get(kit.mb, v + MESH_PIPEDEMO_K_P99_US) >= mesh_mailbox_get(kit.mb, v + MESH_PIPEDEMO_K_P50_US));
    mesh_kit_close(&kit);
    free(src);
    free(sink);
    free(err);
    return mesh_check_done();
}
