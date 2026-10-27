/* pathtrace-wasm: tiles rendered in the executor equal the CPU reference (@ajax/pathtrace-z, itself
 * bit for bit with the Rust): job records (pass-sum and accumulator digests), the tone-mapped rgb,
 * an OP_PASS item's pass sums, and a scene request. A small image (64 x 48: 4 tiles, 1 sample a
 * pass, depth 1) keeps it quick. */
#include "mesh/abi/ops.h"
#include "mesh/abi/ring.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"
#include "mesh/ftz.h"
#include "pathtrace/scene.h"
#include "pathtrace/view.h"
#include "pathtrace/z.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *PT = "name = \"pathtrace\"\nabi = 6\nview_bytes = 0x41a000\nview_abi = 2\nnodes = 1\npolicy = \"job\"\n"
                        "job_base = 0x100\njob_out = \"0x200:32\"\njob_payload = \"0x6000:9216\"\n"
                        "request = \"0xa00:0x1000,0x1a00:0x100\"\nbackends = [\"wasm\"]\ntask_ms = 60000\n";

static mesh_kit kit;
static uint32_t vu(size_t off) { return mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, 0, off)); }

int main(void) {
    char *wasm = mesh_kit_artifact("ajax-mesh-prog-pathtrace-wasm");
    CHECK(wasm != NULL);
    if (!wasm) return mesh_check_done();
    char *err = NULL;
    CHECK(mesh_kit_open(&kit, NULL, &err));
    if (!mesh_kit_load(&kit, 0, PT, wasm, &err)) {
        fprintf(stderr, "load: %s\n", err);
        CHECK(false);
        return mesh_check_done();
    }
    uint32_t r, fault;
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_INIT, 0, &r, &fault, 60000));
    CHECK_U(vu(PT_V_MAGIC), PT_VIEW_MAGIC);
    CHECK(mesh_kit_task(&kit, 0, PT_OP_SET, (uint32_t)PT_H_WIDTH << 24 | 64, &r, &fault, 60000));
    CHECK_U(r, MESH_R_NONE);
    CHECK(mesh_kit_task(&kit, 0, PT_OP_SET, (uint32_t)PT_H_HEIGHT << 24 | 48, &r, &fault, 60000));
    CHECK(mesh_kit_task(&kit, 0, PT_OP_SET, (uint32_t)PT_H_SPP << 24 | 1, &r, &fault, 60000));
    CHECK(mesh_kit_task(&kit, 0, PT_OP_SET, (uint32_t)PT_H_DEPTH << 24 | 1, &r, &fault, 60000));
    CHECK(mesh_kit_task(&kit, 0, PT_OP_SET, (uint32_t)PT_H_MAGIC << 24 | 1, &r, &fault, 60000));
    CHECK_U(r, MESH_R_BAD_OP); /* not settable */
    CHECK(mesh_kit_task(&kit, 0, PT_OP_RESTART, 0, &r, &fault, 60000));
    CHECK_U(r, MESH_R_NONE);
    CHECK_U(vu(PT_V_TILES), 4);
    /* The scene as the program sees it, for the reference. */
    uint32_t sc[PT_SCENE_MAX_WORDS];
    uint32_t words = vu(PT_V_SCENE + 4 * PT_H_WORDS);
    for (uint32_t i = 0; i < words; i++) sc[i] = vu(PT_V_SCENE + 4 * i);
    mesh_mailbox_set64(kit.mb, mesh_kit_view(&kit, 0, PT_V_JOB_BASE), 0);
    static pt_v part[PT_TILE_PIX], acc[PT_TILE_PIX];
    static uint32_t rgb[PT_TILE_PIX];
    for (uint32_t t = 0; t < 4; t++) {
        uint32_t tk = kit.slot[0].next;
        CHECK(mesh_kit_task(&kit, 0, MESH_OP_JOB, t, &r, &fault, 60000));
        memset(part, 0, sizeof part);
        memset(acc, 0, sizeof acc);
        uint64_t dp = pt_tile_pass_z(sc, t, 0, part), da, dr;
        pt_tile_fold_z(sc, 1, PT_TONEMAP_ACES, acc, part, rgb, &da, &dr);
        CHECK_U(r, (uint32_t)dp & 0x7fffffffu);
        size_t rec = PT_V_JOB_OUT + PT_JOB_REC * (tk % MESH_PROG_RESULTS);
        CHECK_U(vu(rec), t);
        CHECK_U(vu(rec + 4), 0);
        CHECK_U((uint64_t)vu(rec + 8) | (uint64_t)vu(rec + 12) << 32, dp);
        CHECK_U((uint64_t)vu(rec + 16) | (uint64_t)vu(rec + 20) << 32, da);
        uint32_t bad = 0;
        for (uint32_t i = 0; i < PT_TILE_PIX; i++) bad += vu(pt_view_rgb(t) + 4 * i) != rgb[i];
        CHECK_U(bad, 0);
        CHECK_U(vu(pt_view_meta(t) + PT_M_PASSES), 1);
    }
    CHECK_U(vu(PT_V_ITEMS), 4);
    /* OP_PASS: tile 1, pass 1 (item 5), pass sums only. */
    uint32_t tk = kit.slot[0].next;
    CHECK(mesh_kit_task(&kit, 0, PT_OP_PASS, 5, &r, &fault, 60000));
    memset(part, 0, sizeof part);
    uint64_t dp = pt_tile_pass_z(sc, 1, 1, part);
    CHECK_U(r, (uint32_t)dp & 0x7fffffffu);
    size_t pay = pt_view_payload(tk % MESH_PROG_RESULTS);
    CHECK_U(vu(pay), mesh_ftz_bits(part[0].x));
    CHECK_U(vu(pay + 12 * (PT_TILE_PIX - 1) + 8), mesh_ftz_bits(part[PT_TILE_PIX - 1].z));
    /* Out of range: the job's leader retries elsewhere. */
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_JOB, 1000, &r, &fault, 60000));
    CHECK_U(r, MESH_R_JOB_FAIL);
    /* A scene request: the scene back as raw words, validated and loaded. */
    char out[256];
    long n = mesh_kit_request(&kit, 0, sc, 4 * words, out, sizeof out - 1, 60000);
    out[n > 0 ? n : 0] = 0;
    CHECK(strncmp(out, "ok scene gen ", 13) == 0 && strstr(out, " tiles 4\n") != NULL);
    n = mesh_kit_request(&kit, 0, sc, 4 * words - 2, out, sizeof out - 1, 60000);
    out[n > 0 ? n : 0] = 0;
    CHECK_S(out, "err scene 1\n");
    mesh_kit_close(&kit);
    free(wasm);
    free(err);
    return mesh_check_done();
}
