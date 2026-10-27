/* Not a check of results (the other tests do that): the wasm executor's rates, printed for the
 * README beside the Rust's (wasmtime) measurements: hello's publish -> result latency and tasks/s,
 * the host-call costs (one mesh::ld32, one 4 KiB mesh::read: kcheck W1), and sha256d's hash rate
 * on one lane (one 2^16-nonce chunk in release, 2^10 in debug). */
#define _XOPEN_SOURCE 700
#include "mesh/abi/ops.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"
#include "mesh/lease.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool release(void) {
    char cmd[512] = {0};
    FILE *f = fopen("/proc/self/cmdline", "r");
    bool r = f && fgets(cmd, sizeof cmd, f) && strstr(cmd, "/release/");
    if (f) fclose(f);
    return r;
}

int main(void) {
    char *hello = mesh_kit_artifact("ajax-mesh-prog-hello-wasm"), *sha = mesh_kit_artifact("ajax-mesh-prog-sha256d-wasm");
    CHECK(hello && sha);
    if (!hello || !sha) return mesh_check_done();
    bool rel = release();
    mesh_kit kit;
    char *err = NULL;
    CHECK(mesh_kit_open(&kit, NULL, &err));
    CHECK(mesh_kit_load(&kit, 0, "name = \"hello\"\nabi = 6\nview_bytes = 0x1000\nnodes = 1\npolicy = \"on_demand\"\n"
                                 "backends = [\"wasm\"]\ntask_ms = 60000\n", hello, &err));
    uint32_t r, fault;
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_INIT, 0, &r, &fault, 60000));
    uint64_t t0 = mesh_host_ns();
    for (int i = 0; i < 1000; i++) CHECK(mesh_kit_task(&kit, 0, MESH_OP_TASK, (uint32_t)i, &r, &fault, 60000));
    double lat = (double)(mesh_host_ns() - t0) / 1000 / 1000;
    t0 = mesh_host_ns();
    CHECK(mesh_kit_tasks(&kit, 0, MESH_OP_TASK, 1, 20000, &fault, 60000));
    double rate = 20000.0 / ((double)(mesh_host_ns() - t0) / 1e9);
    t0 = mesh_host_ns();
    CHECK(mesh_kit_task(&kit, 0, 8, 1000000, &r, &fault, 60000));
    double ld = (double)(mesh_host_ns() - t0) / 1e6;
    t0 = mesh_host_ns();
    CHECK(mesh_kit_task(&kit, 0, 9, 10000, &r, &fault, 60000));
    double rd = (double)(mesh_host_ns() - t0) / 1e4;
    fprintf(stderr, "rates (%s): hello publish -> result %.1f us, %.0f tasks/s on 1 lane; mesh::ld32 %.1f ns, 4 KiB mesh::read %.0f ns\n",
            rel ? "release" : "debug", lat, rate, ld, rd);
    mesh_kit_unload(&kit, 0);
    uint32_t clog = rel ? 16 : 10;
    char m[512];
    snprintf(m, sizeof m, "name = \"sha256d\"\nabi = 6\nview_bytes = 0x2000\nnodes = 1\npolicy = \"job\"\njob_base = 0x1000\n"
             "job_out = \"0x1800:32\"\nbackends = [\"wasm\"]\nparam = %u\ntask_ms = 60000\n", clog);
    if (!CHECK(mesh_kit_load(&kit, 1, m, sha, &err))) {
        fprintf(stderr, "load: %s\n", err);
        mesh_kit_close(&kit);
        return mesh_check_done();
    }
    CHECK(mesh_kit_task(&kit, 1, MESH_OP_INIT, 0, &r, &fault, 60000));
    t0 = mesh_host_ns();
    CHECK(mesh_kit_task(&kit, 1, MESH_OP_JOB, 0, &r, &fault, 600000));
    double s = (double)(mesh_host_ns() - t0) / 1e9;
    fprintf(stderr, "rates (%s): sha256d one lane, a 2^%u chunk in %.1f ms: %.3f MH/s\n", rel ? "release" : "debug", clog,
            s * 1e3, (double)(1u << clog) / s / 1e6);
    mesh_kit_close(&kit);
    free(hello);
    free(sha);
    free(err);
    return mesh_check_done();
}
