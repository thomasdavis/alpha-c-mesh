/* sha256d on the simulator against @ajax/mesh-ref-sha256d: OP_INIT's genesis
 * header and midstate; chunks ground on a 16-warp node, the node's best hash
 * and every share (target 8 zero bits) equal to hashing each nonce on the CPU;
 * the genesis block's own nonce found as a share with Bitcoin's genesis hash;
 * config ops, stale chunks, the param rule, and the request window. */
#include "mesh/check.h"
#include "mesh/ref/sha256d.h"
#include "mesh/rt/sim_mailbox.h"

#include "../src/sha256d.c"

#include <stdlib.h>

#define CLOG 10u

static uint8_t *mb, *view;

static void le_bytes(const uint32_t *w, size_t n, uint8_t *out) {
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 4; k++) out[4 * i + k] = (uint8_t)(w[i] >> (8 * k));
}
static void view_bytes(uint32_t off, uint32_t n, uint8_t *out) { for (uint32_t i = 0; i < n; i++) out[i] = view[off + i]; }

/* One chunk (OP_JOB) against the CPU: shares, best, the job record. */
static void check_chunk(uint64_t g, uint32_t zbits) {
    uint8_t hdr[80], target[32];
    view_bytes(SHA_V_HEADER, 80, hdr);
    mesh_sha_zbits_target(zbits, target);
    uint32_t base = mesh_sim_u32(view + SHA_V_BASE), time = mesh_sim_u32(view + SHA_V_HEADER + 68);
    uint64_t p = g << CLOG;
    uint32_t nonce0 = base + (uint32_t)p, ntime = time + (uint32_t)(p >> 32);
    uint8_t best[32], h[32];
    memset(best, 0xff, 32);
    uint32_t best_nonce = 0, shares = 0, share_nonce[8] = {0};
    for (uint32_t i = 0; i < (1u << CLOG); i++) {
        mesh_sha_hash_at(hdr, nonce0 + i, ntime, h);
        if (mesh_sha_le_lt(h, best)) { memcpy(best, h, 32); best_nonce = nonce0 + i; }
        if (!mesh_sha_le_lt(target, h)) { if (shares < 8) share_nonce[shares] = nonce0 + i; shares++; }
    }
    uint32_t shares0 = mesh_sim_u32(view + SHA_V_SHARES);
    mesh_sim_set_u64(view + SHA_V_JOB_BASE, g);
    uint32_t t = mesh_sim_publish(mb, 0, MESH_OP_JOB, 0), r = 0;
    CHECK(mesh_sim_wait(mb, 0, t, &r, 120000));
    CHECK_U(r, shares);
    CHECK_U(mesh_sim_u32(view + SHA_V_SHARES) - shares0, shares);
    uint32_t rec = SHA_V_JOB_OUT + SHA_JOB_REC * (t % MESH_PROG_RESULTS);
    CHECK_U(mesh_sim_u32(view + rec), shares);
    for (uint32_t i = 0; i < shares && i < SHA_JOB_SHARES; i++) CHECK_U(mesh_sim_u32(view + rec + 4 + 8 * i), share_nonce[i]);
    CHECK_U(mesh_sim_u32(view + rec + 4 + 8 * SHA_JOB_SHARES), ntime);
    /* The view's best is the least so far; this chunk's best matches if it is the best. */
    uint8_t vb[32];
    view_bytes(SHA_V_BEST, 32, vb);
    if (memcmp(vb, best, 32) == 0) CHECK_U(mesh_sim_u32(view + SHA_V_BEST_NONCE), best_nonce);
    else CHECK(mesh_sha_le_lt(vb, best));
}

int main(void) {
    mb = mesh_sim_mailbox();
    mesh_sim_prog p = {.name = "sha256d", .view_bytes = 0x2000, .nodes = 1, .warps = 16, .shared_bytes = 0x60, .param = CLOG,
                       .req_off = 0x800, .req_max = 0x200, .resp_off = 0xa00, .resp_max = 0x200};
    view = mesh_sim_install(mb, 0, &p);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, 0, &p, mesh_main);
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_INIT, 0), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view + SHA_V_MAGIC), SHA_MAGIC);
    CHECK_U(mesh_sim_u32(view + SHA_V_CHUNK_LOG2), CLOG);
    CHECK_U(mesh_sim_u32(view + SHA_V_TARGET_ZBITS), SHA_ZBITS_DEFAULT);
    uint8_t g_hdr[80];
    le_bytes(SHA_GENESIS, 20, g_hdr);
    uint8_t hdr[80];
    view_bytes(SHA_V_HEADER, 80, hdr);
    CHECK(memcmp(hdr, g_hdr, 80) == 0);
    CHECK_U(mesh_sim_u32(view + SHA_V_NBITS), 0x1d00ffffu);
    /* The midstate: the first block's compression (sha256-compress). */
    uint32_t st[8], blk[16];
    memcpy(st, SHA256_H0, sizeof st);
    for (int k = 0; k < 16; k++) blk[k] = (uint32_t)g_hdr[4 * k] << 24 | (uint32_t)g_hdr[4 * k + 1] << 16 | (uint32_t)g_hdr[4 * k + 2] << 8 | g_hdr[4 * k + 3];
    sha256_compress(st, blk);
    for (int k = 0; k < 8; k++) CHECK_U(mesh_sim_u32(view + SHA_V_MID + 4 * k), st[k]);
    /* Chunks at 8 zero bits: a few shares each. */
    CHECK_U(mesh_sim_call(mb, 0, SHA_OP_ZBITS, 8), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view + SHA_V_TARGET_ZBITS), 8);
    for (uint64_t g = 0; g < 3; g++) check_chunk(g, 8);
    check_chunk((uint64_t)1 << 22, 8); /* time rolled: p >> 32 = 1 */
    CHECK_U(mesh_sim_u64(view + SHA_V_HASHES), 4u << CLOG);
    /* The genesis nonce, with the header's own target (nbits 1d00ffff): a share,
     * and its hash is the genesis block's. */
    CHECK_U(mesh_sim_call(mb, 0, SHA_OP_GENESIS, 0), MESH_R_NONE);
    CHECK_U(mesh_sim_call(mb, 0, SHA_OP_BASE, MESH_SHA_GENESIS_NONCE - 100), MESH_R_NONE);
    mesh_sim_set_u64(view + SHA_V_JOB_BASE, 0);
    uint32_t t = mesh_sim_publish(mb, 0, MESH_OP_JOB, 0), r = 0;
    CHECK(mesh_sim_wait(mb, 0, t, &r, 120000));
    CHECK_U(r, 1);
    CHECK_U(mesh_sim_u32(view + SHA_V_LAST_NONCE), MESH_SHA_GENESIS_NONCE);
    CHECK_U(mesh_sim_u32(view + SHA_V_BEST_NONCE), MESH_SHA_GENESIS_NONCE);
    uint8_t best[32];
    char disp[65];
    view_bytes(SHA_V_BEST, 32, best);
    mesh_sha_display(best, disp);
    CHECK_S(disp, MESH_SHA_GENESIS_HASH);
    /* Bad config args; the chunk op is refused. */
    CHECK_U(mesh_sim_call(mb, 0, SHA_OP_ZBITS, 0), MESH_R_BAD_OP);
    CHECK_U(mesh_sim_call(mb, 0, SHA_OP_CHUNK, 12), MESH_R_BAD_OP);
    CHECK_U(mesh_sim_call(mb, 0, SHA_OP_NBITS, 0x1d80ffffu), MESH_R_BAD_OP); /* negative */
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_TASK, 0), MESH_R_BAD_OP);
    /* The request window. */
    char out[512];
    uint32_t n = 0;
    const char *st_req = "status";
    mesh_sim_request(mb, 0, st_req, (uint32_t)strlen(st_req), out, sizeof out - 1, &n);
    CHECK(n != UINT32_MAX);
    out[n] = 0;
    char want[256];
    snprintf(want, sizeof want, " zbits=32 base=%u chunk_log2=10 time=1231006505 hashes=", MESH_SHA_GENESIS_NONCE - 100);
    CHECK(strncmp(out, "ok gen=", 7) == 0 && strstr(out, want) != NULL);
    CHECK(strstr(out, " best=" MESH_SHA_GENESIS_HASH "\n") != NULL);
    const char *bad = "zbits 300";
    mesh_sim_request(mb, 0, bad, (uint32_t)strlen(bad), out, sizeof out - 1, &n);
    out[n] = 0;
    CHECK_S(out, "err zbits: 1..=255\n");
    const char *ch = "chunk 12";
    mesh_sim_request(mb, 0, ch, (uint32_t)strlen(ch), out, sizeof out - 1, &n);
    out[n] = 0;
    CHECK_S(out, "err chunk: fixed by the manifest's param (one chunk size on every member of a cluster)\n");
    const char *zr = "zbits 12 base 5 reset";
    mesh_sim_request(mb, 0, zr, (uint32_t)strlen(zr), out, sizeof out - 1, &n);
    out[n] = 0;
    CHECK(strncmp(out, "ok gen=", 7) == 0 && strstr(out, " zbits=12 base=5 ") != NULL);
    CHECK_U(mesh_sim_u64(view + SHA_V_HASHES), 0);
    /* A raw 80-byte header (not starting with a lowercase letter). */
    mesh_sim_request(mb, 0, g_hdr, 80, out, sizeof out - 1, &n);
    out[n] = 0;
    CHECK(strncmp(out, "ok gen=", 7) == 0);
    /* A param outside 6..=24 fails every chunk. */
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, 0) + MESH_P_FAULT), 0);
    mesh_sim_halt(mb, 0, l);
    mesh_sim_prog q = p;
    q.param = 5;
    mesh_sim_install(mb, 0, &q);
    l = mesh_sim_launch_prog(mb, 0, &q, mesh_main);
    CHECK_U(mesh_sim_call(mb, 0, MESH_OP_JOB, 0), MESH_R_JOB_FAIL);
    mesh_sim_halt(mb, 0, l);
    mesh_sim_mailbox_free(mb);
    return mesh_check_done();
}
