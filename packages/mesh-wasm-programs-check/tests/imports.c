/* The mesh::* imports through a guest (programs/probe-wasm), against mesh-host/src/wasm.rs's
 * linker: windows, bounds faults (FAULT_VIEW_BOUNDS | off << 8, the first kept), peer views gated
 * by `reads` (FAULT_PEER_VIEW), find, prog_state, counters, atomics, bulk copies, the input queue,
 * the request window and respond (FAULT_RESP_BOUNDS), exchange outside lockstep, info. */
#include "mesh/abi/control_block.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/input_queue.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/ring.h"
#include "mesh/abi/wasm.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"
#include "mesh/lease.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PROBE = "name = \"probe\"\nabi = 6\nview_bytes = 0x100\nnodes = 1\npolicy = \"on_demand\"\n"
                           "request = \"0x40:0x20,0x60:0x20\"\ninput = \"0x80:16\"\nfloat = \"ieee\"\nparam = 77\n"
                           "backends = [\"wasm\"]\ntask_ms = 60000\n";
static const char *OTHER = "name = \"other\"\nabi = 6\nview_bytes = 0x200\nnodes = 1\npolicy = \"on_demand\"\n"
                           "float = \"ieee\"\nbackends = [\"wasm\"]\ntask_ms = 60000\n";

static mesh_kit kit;
static char *wasm;

static uint32_t fault_of(uint32_t slot) { return mesh_mailbox_get(kit.mb, mesh_prog_ctl(slot) + MESH_P_FAULT); }

/* Runs (op, arg) on a fresh load of the probe; the result and the fault after it. */
static uint32_t once(uint32_t op, uint32_t arg, uint32_t *fault) {
    char *err = NULL;
    mesh_kit_unload(&kit, 0);
    CHECK(mesh_kit_load(&kit, 0, PROBE, wasm, &err));
    free(err);
    uint32_t r = 0, f = 0, t = kit.slot[0].next;
    if (!mesh_kit_task(&kit, 0, op, arg, &r, &f, 60000)) {
        /* A fault recorded mid-task: the task still completes; its result is in P_RESULTS. */
        size_t mark = mesh_prog_ctl(0) + MESH_P_MARKS + 4 * (size_t)(t % 64);
        uint64_t t0 = mesh_host_ns();
        while (mesh_mailbox_get(kit.mb, mark) != t + 1 && mesh_host_ns() - t0 < 1000000000ull) {
        }
        r = mesh_mailbox_get(kit.mb, mesh_prog_ctl(0) + MESH_P_RESULTS + 4 * (size_t)(t % 64));
    }
    *fault = fault_of(0);
    return r;
}

int main(void) {
    wasm = mesh_kit_artifact("ajax-mesh-prog-probe-wasm");
    CHECK(wasm != NULL);
    if (!wasm) return mesh_check_done();
    char *err = NULL;
    CHECK(mesh_kit_open(&kit, NULL, &err));
    CHECK(mesh_kit_load(&kit, 3, OTHER, wasm, &err));
    uint32_t f, r;
    /* ld32 inside, at the last word, past it, misaligned. */
    r = once(17, 0xfc, &f);
    CHECK_U(f, 0);
    r = once(17, 0x100, &f);
    CHECK_U(r, 0);
    CHECK_U(f, MESH_FAULT_VIEW_BOUNDS | 0x100u << 8);
    r = once(17, 2, &f);
    CHECK_U(f, MESH_FAULT_VIEW_BOUNDS | 2u << 8);
    r = once(13, 0, &f);
    CHECK_U(f, MESH_FAULT_VIEW_BOUNDS | 4u << 8);
    /* An unknown window: FAULT_VIEW_BOUNDS; a peer window never resolved: FAULT_PEER_VIEW. */
    /* peer: not in reads -> -1 and FAULT_PEER_VIEW | s << 8; once allowed, the view's length. */
    r = once(3, 3, &f);
    CHECK_U(r, 0xffffffffu);
    CHECK_U(f, MESH_FAULT_PEER_VIEW | 3u << 8);
    mesh_kit_unload(&kit, 0);
    CHECK(mesh_kit_load(&kit, 0, PROBE, wasm, &err));
    mesh_kit_reads(&kit, 0, 1u << 3);
    CHECK(mesh_kit_task(&kit, 0, 3, 3, &r, &f, 60000));
    CHECK_U(r, 0x200);
    CHECK_U(fault_of(0), 0);
    /* find: the probe's own slot; prog_state. */
    r = once(4, 0, &f);
    CHECK_U(r, 0);
    CHECK(mesh_kit_task(&kit, 0, 9, 3, &r, &f, 60000));
    CHECK_U(r, MESH_ST_RUNNING);
    CHECK(mesh_kit_task(&kit, 0, 9, 99, &r, &f, 60000));
    CHECK_U(r, MESH_ST_EMPTY);
    /* Counters, a byte, atomics, bulk copies, info. */
    CHECK(mesh_kit_task(&kit, 0, 10, 5, &r, &f, 60000));
    CHECK(mesh_kit_task(&kit, 0, 10, 6, &r, &f, 60000));
    CHECK_U(r, 11);
    CHECK(mesh_kit_task(&kit, 0, 11, 0x33, &r, &f, 60000));
    CHECK_U(r, 0xab);
    CHECK(mesh_kit_task(&kit, 0, 12, 0, &r, &f, 60000));
    CHECK_U(r, 0 * 100 + 7 + 0 + 12 * 1000);
    CHECK(mesh_kit_task(&kit, 0, 15, 3, &r, &f, 60000));
    CHECK_U(r, 32);
    CHECK(mesh_kit_task(&kit, 0, 14, MESH_WASM_I_PARAM, &r, &f, 60000));
    CHECK_U(r, 77);
    CHECK(mesh_kit_task(&kit, 0, 14, MESH_WASM_I_VIEW_BYTES, &r, &f, 60000));
    CHECK_U(r, 0x100);
    CHECK(mesh_kit_task(&kit, 0, 14, MESH_WASM_I_INPUT_LEN, &r, &f, 60000));
    CHECK_U(r, 16);
    CHECK(mesh_kit_task(&kit, 0, 14, 999, &r, &f, 60000));
    CHECK_U(r, 0);
    CHECK_U(fault_of(0), 0);
    /* The input queue: three bytes queued by the host, popped in order. */
    size_t q = mesh_kit_view(&kit, 0, 0x80);
    mesh_mailbox_set8(kit.mb, q + MESH_IQ_DATA + 0, 'a');
    mesh_mailbox_set8(kit.mb, q + MESH_IQ_DATA + 1, 'b');
    mesh_mailbox_set8(kit.mb, q + MESH_IQ_DATA + 2, 'c');
    mesh_mailbox_publish(kit.mb, q + MESH_IQ_HEAD, 3);
    CHECK(mesh_kit_task(&kit, 0, 6, 0, &r, &f, 60000));
    CHECK_U(r >> 24, 3);
    CHECK_U(r & 0xffffff, (uint32_t)(('a' * 31 + 'b') * 31 + 'c'));
    CHECK_U(mesh_mailbox_get(kit.mb, q + MESH_IQ_TAIL), 3);
    /* The request window: echoed upper-cased as the response. */
    char out[64];
    long n = mesh_kit_request(&kit, 0, "hello wasm", 10, out, sizeof out - 1, 60000);
    out[n > 0 ? n : 0] = 0;
    CHECK_S(out, "HELLO WASM");
    /* respond past resp_max: FAULT_RESP_BOUNDS | len << 8, clamped. */
    r = once(5, 0x21, &f);
    CHECK_U(f, MESH_FAULT_RESP_BOUNDS | 0x21u << 8);
    CHECK_U(mesh_mailbox_get(kit.mb, mesh_prog_ctl(0) + MESH_P_RESP_LEN), 0x20);
    /* exchange outside lockstep: ROUND_TIMEOUT and FAULT_PEER_VIEW | 0xff << 8. */
    r = once(7, 0, &f);
    CHECK_U(r, MESH_ROUND_TIMEOUT);
    CHECK_U(f, MESH_FAULT_PEER_VIEW | 0xffu << 8);
    /* peer of a slot `reads` doesn't list (only 3 is, after the reads change below: none here). */
    r = once(3, 7, &f);
    CHECK_U(r, 0xffffffffu);
    CHECK_U(f, MESH_FAULT_PEER_VIEW | 7u << 8);
    /* read into linear memory past its end: FAULT_VIEW_BOUNDS | off << 8 (off 0). */
    r = once(8, 0, &f);
    CHECK_U(f, MESH_FAULT_VIEW_BOUNDS);
    mesh_kit_close(&kit);
    free(wasm);
    free(err);
    return mesh_check_done();
}
