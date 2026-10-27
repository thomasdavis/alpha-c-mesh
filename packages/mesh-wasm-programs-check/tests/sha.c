/* sha256d-wasm and sha-verify-wasm: the request language, finding Bitcoin's genesis block (its
 * nonce in a leased chunk, its hash, its record), shares at a low target checked against the CPU
 * reference (mesh-ref-sha256d), and sha-verify re-hashing them through `reads`. */
#include "mesh/abi/control_block.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/ring.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"
#include "mesh/ref/sha256d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SHA = "name = \"sha256d\"\nabi = 6\nview_bytes = 0x2000\nview_abi = 1\nnodes = 2\npolicy = \"job\"\n"
                         "fanout = 2\njob_base = 0x1000\njob_out = \"0x1800:32\"\nrequest = \"0x800:0x200,0xa00:0x200\"\n"
                         "backends = [\"wasm\"]\nparam = 8\ntask_ms = 60000\n";
static const char *VERIFY = "name = \"sha-verify\"\nabi = 6\nbackends = [\"wasm\"]\nfloat = \"ieee\"\nview_bytes = 0x100\n"
                            "nodes = 1\npolicy = \"periodic\"\nhz = 50\nreads = [\"sha256d\"]\ntask_ms = 60000\n";

static mesh_kit kit;

static uint32_t vu(uint32_t slot, uint32_t off) { return mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, slot, off)); }

static void header(uint8_t out[80]) {
    for (int i = 0; i < 80; i++) out[i] = mesh_mailbox_get8(kit.mb, mesh_kit_view(&kit, 0, 0x080 + (size_t)i));
}

int main(void) {
    char *sha = mesh_kit_artifact("ajax-mesh-prog-sha256d-wasm"), *verify = mesh_kit_artifact("ajax-mesh-prog-sha-verify-wasm");
    CHECK(sha && verify);
    if (!sha || !verify) return mesh_check_done();
    char *err = NULL;
    CHECK(mesh_kit_open(&kit, NULL, &err));
    if (!mesh_kit_load(&kit, 0, SHA, sha, &err)) {
        fprintf(stderr, "load: %s\n", err);
        CHECK(false);
        return mesh_check_done();
    }
    uint32_t r, fault;
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_INIT, 0, &r, &fault, 60000));
    CHECK_U(vu(0, 0x000), 0x36353253u);
    CHECK_U(vu(0, 0x028), 26);
    CHECK_U(vu(0, 0x024), 8);
    char out[512];
    long n = mesh_kit_request(&kit, 0, "status", 6, out, sizeof out - 1, 60000);
    CHECK(n > 0);
    out[n > 0 ? n : 0] = 0;
    CHECK(strncmp(out, "ok gen=2 zbits=26 base=0 chunk_log2=8 time=1231006505 hashes=0 shares=0 best_zbits=0 best_nonce=0 best=ffff", 107) == 0);
    n = mesh_kit_request(&kit, 0, "zbits 999", 9, out, sizeof out - 1, 60000);
    out[n > 0 ? n : 0] = 0;
    CHECK_S(out, "err zbits: 1..=255\n");
    n = mesh_kit_request(&kit, 0, "frobnicate", 10, out, sizeof out - 1, 60000);
    out[n > 0 ? n : 0] = 0;
    CHECK_S(out, "err unknown word (header target zbits nbits base time genesis reset status)\n");

    /* The genesis block: nbits 1d00ffff from its header, the nonce 5 into a 256-nonce chunk. */
    CHECK(mesh_kit_task(&kit, 0, 6, 0, &r, &fault, 60000));
    CHECK_U(r, MESH_R_NONE);
    CHECK(mesh_kit_task(&kit, 0, 8, MESH_SHA_GENESIS_NONCE - 5, &r, &fault, 60000));
    mesh_mailbox_set64(kit.mb, mesh_kit_view(&kit, 0, 0x1000), 0);
    uint32_t t = kit.slot[0].next;
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_JOB, 0, &r, &fault, 60000));
    CHECK_U(r, 1);
    size_t rec = mesh_kit_view(&kit, 0, 0x1800 + 32 * (t % MESH_PROG_RESULTS));
    CHECK_U(mesh_mailbox_get(kit.mb, rec), 1);
    CHECK_U(mesh_mailbox_get(kit.mb, rec + 4), MESH_SHA_GENESIS_NONCE);
    uint8_t hdr[80], h[32];
    header(hdr);
    mesh_sha_hash_at(hdr, MESH_SHA_GENESIS_NONCE, vu(0, 0x048), h);
    char disp[65];
    mesh_sha_display(h, disp);
    CHECK_S(disp, MESH_SHA_GENESIS_HASH);
    CHECK_U(mesh_mailbox_get(kit.mb, rec + 8), mesh_sha_zbits(h));
    uint8_t ring[32];
    for (int i = 0; i < 32; i++) ring[i] = mesh_mailbox_get8(kit.mb, mesh_kit_view(&kit, 0, 0x200 + 16 + (size_t)i));
    CHECK(memcmp(ring, h, 32) == 0);
    CHECK_U(vu(0, 0x02c), 1);

    /* A low target: every share of a few chunks is a real one. */
    CHECK(mesh_kit_task(&kit, 0, 3, 6, &r, &fault, 60000));
    CHECK(mesh_kit_task(&kit, 0, 8, 0, &r, &fault, 60000));
    uint32_t total = 0;
    for (uint32_t c = 0; c < 4; c++) {
        CHECK(mesh_kit_task(&kit, 0, MESH_OP_JOB, c, &r, &fault, 60000));
        total += r;
    }
    CHECK(total > 4); /* 1024 nonces at 1 in 64 */
    CHECK_U(vu(0, 0x02c), total);
    uint8_t target[32];
    for (int i = 0; i < 32; i++) target[i] = mesh_mailbox_get8(kit.mb, mesh_kit_view(&kit, 0, 0x0d0 + (size_t)i));
    header(hdr);
    uint32_t good = 0;
    uint32_t first = total > 32 ? total - 32 : 0;
    for (uint32_t s = first; s < total; s++) {
        size_t e = mesh_kit_view(&kit, 0, 0x200 + (s % 32) * 48);
        uint32_t nonce = mesh_mailbox_get(kit.mb, e), ntime = mesh_mailbox_get(kit.mb, e + 4);
        CHECK_U(mesh_mailbox_get(kit.mb, e + 8), s + 1);
        uint8_t got[32];
        for (int i = 0; i < 32; i++) got[i] = mesh_mailbox_get8(kit.mb, e + 16 + (size_t)i);
        mesh_sha_hash_at(hdr, nonce, ntime, h);
        if (memcmp(got, h, 32) == 0 && !mesh_sha_le_lt(target, h)) good++;
    }
    CHECK_U(good, total - first);

    /* sha-verify reads sha256d's view and re-hashes the shares still in the ring. */
    CHECK(mesh_kit_load(&kit, 1, VERIFY, verify, &err));
    CHECK(mesh_kit_task(&kit, 1, MESH_OP_INIT, 0, &r, &fault, 60000));
    CHECK(mesh_kit_task(&kit, 1, MESH_OP_TASK, 0, &r, &fault, 60000));
    CHECK_U(r, total - first);
    CHECK_U(vu(1, 0x00), total - first);
    CHECK_U(vu(1, 0x04), 0);
    CHECK_U(vu(1, 0x08), first);
    CHECK_U(vu(1, 0x0c), total);
    mesh_kit_close(&kit);
    free(sha);
    free(verify);
    free(err);
    return mesh_check_done();
}
