/* sha-verify: re-hashes sha256d's shares on the CPU (kcheck's S checks, run continuously beside
 * the GPU), from alpha-rust-mesh programs/sha-verify/wasm. Each periodic task reads sha256d's view
 * through `reads`, and for every share number past the last one checked takes its ring entry
 * (nonce, ntime, number, zbits, hash), recomputes the header's sha256d with mesh-ref, and counts
 * it verified when the hashes are equal and the hash is at most the target, else bad.
 * Configuration changes (sha256d's `gen`, odd while written) restart the count; a share that left
 * the 32-entry ring before its check is `missed`. The ring entry is read twice: one that a node
 * rewrote meanwhile is checked at the next task. */
#include "mesh/ref/sha256d.h"
#include "mesh/rt_wasm.h"

/* sha256d's view (programs/sha256d V_*). */
#define S_GEN 0x004u
#define S_SHARES 0x02cu
#define S_HEADER 0x080u
#define S_TARGET 0x0d0u
#define S_RING 0x200u
#define RING 32u
#define RING_BYTES 48u

#define V_VERIFIED 0x00u
#define V_BAD 0x04u
#define V_MISSED 0x08u
#define V_CHECKED 0x0cu
#define V_GEN 0x10u
#define V_LAST_NONCE 0x14u

static void init(const mesh_ctx *ctx) { (void)ctx; }

static uint32_t w32(const uint8_t *x, uint32_t i) {
    return (uint32_t)x[4 * i] | (uint32_t)x[4 * i + 1] << 8 | (uint32_t)x[4 * i + 2] << 16 | (uint32_t)x[4 * i + 3] << 24;
}

static uint32_t check(const mesh_ctx *ctx) {
    uint32_t s;
    mesh_view v;
    if (!mesh_ctx_find(ctx, "sha256d", 7, &s) || !mesh_ctx_peer_view(ctx, s, &v)) return 0;
    uint32_t gen = mesh_view_u32(v, S_GEN);
    if (gen & 1) return 0;
    uint32_t shares = mesh_view_u32(v, S_SHARES);
    uint32_t done = mesh_ctx_view_u32(ctx, V_CHECKED);
    if (gen != mesh_ctx_view_u32(ctx, V_GEN) || shares < done) {
        done = 0;
        mesh_ctx_set_u32(ctx, V_GEN, gen);
    }
    uint8_t hdr[80], target[32];
    mesh_view_read(v, S_HEADER, hdr, sizeof hdr);
    mesh_view_read(v, S_TARGET, target, sizeof target);
    uint32_t ok = 0, bad = 0, missed = 0;
    while (done < shares) {
        uint32_t n = done; /* share number n (0-based) is ring entry n mod RING, its field n + 1 */
        if (shares - n > RING) {
            missed++;
            done++;
            continue;
        }
        uint32_t e = S_RING + (n % RING) * RING_BYTES;
        uint8_t a[48], b[48];
        mesh_view_read(v, e, a, sizeof a);
        mesh_view_read(v, e, b, sizeof b);
        bool same = true;
        for (int i = 0; i < 48; i++) same = same && a[i] == b[i];
        if (!same || w32(a, 2) != n + 1u) break;
        uint8_t h[32];
        mesh_sha_hash_at(hdr, w32(a, 0), w32(a, 1), h);
        bool eq = true;
        for (int i = 0; i < 32; i++) eq = eq && h[i] == a[16 + i];
        if (eq && !mesh_sha_le_lt(target, h)) ok++;
        else bad++;
        mesh_ctx_set_u32(ctx, V_LAST_NONCE, w32(a, 0));
        done++;
    }
    /* A configuration change meanwhile: these were checked against another header; count none. */
    if (mesh_view_u32(v, S_GEN) != gen) return 0;
    mesh_ctx_set_u32(ctx, V_CHECKED, done);
    const uint32_t offs[3] = {V_VERIFIED, V_BAD, V_MISSED}, xs[3] = {ok, bad, missed};
    for (uint32_t c = 0; c < 3; c++) {
        if (xs[c] > 0) {
            mesh_ctx_set_u32(ctx, offs[c], mesh_ctx_view_u32(ctx, offs[c]) + xs[c]);
            mesh_ctx_count(ctx, c, xs[c]);
        }
    }
    return ok;
}

static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    (void)arg;
    return op == MESH_OP_TASK ? check(ctx) : MESH_R_BAD_OP;
}

MESH_PROGRAM(init, task)
