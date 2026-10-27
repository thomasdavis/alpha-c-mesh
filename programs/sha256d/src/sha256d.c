/* sha256d: Bitcoin-style double SHA-256 nonce grinding.
 *
 * A job program (plan F5): every chunk of 1 << chunk_log2 nonces is an item of
 * a fleet job, leased by the leader and run as OP_JOB. The threads hash the
 * chunk in parallel, ILP hashes per thread per iteration. The first 64-byte
 * block of the header is constant, so its compression (the midstate) is
 * computed once per header change and kept in the view.
 *
 * Nonce space: chunk g covers position p = g << chunk_log2: nonces base +
 * (p mod 2^32) .. + chunk, with the header time rolled by p >> 32 (ntime =
 * time + roll), so the search never repeats. chunk_log2 is the manifest's
 * `param` (PX_PARAM), so every member of a cluster grinds the same chunk; a
 * param outside CHUNK_LOG2_MIN..=CHUNK_LOG2_MAX fails every OP_JOB. When it
 * changes (an upgrade) the next task restarts the search with the new size.
 *
 * Hash order: compared as a little-endian 256-bit number (Bitcoin): its most
 * significant 32-bit word is bswap(H[7]) of the second SHA-256's state. The
 * view stores hashes and the target as 32 bytes in that order.
 *
 * Hot loop: each thread keeps only its best (hash top 64 bits, nonce) and a
 * candidate flag (top word <= the target's). If any thread saw a candidate,
 * warp 0 rescans the chunk with the full compare and records every share, so
 * the share count is exact and the hot loop has no data-dependent stores.
 *
 * Consistency: configuration is written by lane 0 of whichever node runs the
 * op, under the view's spin lock, with gen odd meanwhile. A task reads the
 * configuration under the gen seqlock and records its results under the lock
 * only if gen is unchanged; a chunk that straddled a change is stale.
 *
 * Ops: OP_JOB grind chunk V_JOB_BASE + arg (its share count, or R_JOB_FAIL);
 * OP_REQ (text or raw, below); 3 target = arg leading zero bits (1..=255); 4
 * reset the stats; 5 target from compact nbits = arg; 6 the genesis header,
 * nonce base 0, target arg zero bits (0: the header's own nbits); 7 refused
 * (the chunk is the manifest's param); 8 nonce base = arg; 9 header time = arg.
 * Request window: whitespace-separated `header <160 hex>`, `target <64 hex,
 * display order>`, `zbits <n>`, `nbits <8 hex>`, `base <n>`, `time <n>`,
 * `genesis`, `reset`, `status`; or raw bytes: 80 (header) or 112 (header,
 * then the target in internal order), when the first byte is not a lowercase
 * letter. The response is one text line (`ok ...` or `err ...`).
 * Ported from alpha-rust-mesh programs/sha256d/src/lib.rs. */
#include "mesh/prog/sha256d.h"
#include "mesh/rt.h"

/* The Bitcoin genesis block header, as 20 little-endian words. */
static const uint32_t SHA_GENESIS[20] = {
    0x00000001u, 0, 0, 0, 0, 0, 0, 0, 0, 0xfdeda33bu, 0xb2127b7au, 0x3e2cc77au, 0x618f7667u, 0xc31bc87fu, 0x32518a88u,
    0xaab89f3au, 0x4a5e1e4bu, 0x495fab29u, 0x1d00ffffu, 0x7c2bac1du,
};

static const uint32_t SHA_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};
static const uint32_t SHA_IV[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

#define SHA_INLINE static inline __attribute__((always_inline))

/* A rotate (a funnel shift, shf.r.wrap.b32, on the GPU). */
SHA_INLINE uint32_t sha_rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << ((32u - n) & 31u)); }
SHA_INLINE uint32_t sha_bsig0(uint32_t a) { return sha_rotr(a, 2) ^ sha_rotr(a, 13) ^ sha_rotr(a, 22); }
SHA_INLINE uint32_t sha_bsig1(uint32_t e) { return sha_rotr(e, 6) ^ sha_rotr(e, 11) ^ sha_rotr(e, 25); }
SHA_INLINE uint32_t sha_ssig0(uint32_t x) { return sha_rotr(x, 7) ^ sha_rotr(x, 18) ^ (x >> 3); }
SHA_INLINE uint32_t sha_ssig1(uint32_t x) { return sha_rotr(x, 17) ^ sha_rotr(x, 19) ^ (x >> 10); }
SHA_INLINE uint32_t sha_ch(uint32_t e, uint32_t f, uint32_t g) { return g ^ (e & (f ^ g)); }
SHA_INLINE uint32_t sha_maj(uint32_t a, uint32_t b, uint32_t c) { return (a & b) | (c & (a | b)); }
/* Byte swap (prmt on the GPU). */
SHA_INLINE uint32_t sha_bswap(uint32_t x) { return __builtin_bswap32(x); }

/* Round i of N independent messages: r = i mod 16 (the schedule window's
 * slot), kc = K[i]. N is ILP (2) or 1; every call is inlined with constants,
 * so the window's slots are registers. */
SHA_INLINE void sha_round(uint32_t s[8][SHA_ILP], uint32_t w[16][SHA_ILP], uint32_t n, uint32_t r, uint32_t kc, bool expand) {
    for (uint32_t j = 0; j < n; j++) {
        if (expand) {
            uint32_t x = w[(r + 14) & 15][j], y = w[(r + 1) & 15][j];
            w[r][j] = w[r][j] + sha_ssig1(x) + w[(r + 9) & 15][j] + sha_ssig0(y);
        }
        uint32_t a = s[0][j], b = s[1][j], c = s[2][j], d = s[3][j], e = s[4][j], f = s[5][j], g = s[6][j], h = s[7][j];
        uint32_t t1 = h + sha_bsig1(e) + sha_ch(e, f, g) + kc + w[r][j];
        uint32_t t2 = sha_bsig0(a) + sha_maj(a, b, c);
        s[7][j] = g;
        s[6][j] = f;
        s[5][j] = e;
        s[4][j] = d + t1;
        s[3][j] = c;
        s[2][j] = b;
        s[1][j] = a;
        s[0][j] = t1 + t2;
    }
}

/* The 64 rounds. Rounds 0..16 (message words, often constant) and 48..64
 * (whose tail the hot loop does not use) are unrolled so constants fold and
 * dead rounds drop; rounds 16..48 are a 2-trip loop over 16 unrolled rounds,
 * which keeps the hot loop's code small enough for the instruction cache. */
SHA_INLINE void sha_rounds64(uint32_t s[8][SHA_ILP], uint32_t w[16][SHA_ILP], uint32_t n) {
    for (uint32_t r = 0; r < 16; r++) sha_round(s, w, n, r, SHA_K[r], false);
#if defined(__clang__)
#pragma clang loop unroll(disable)
#endif
    for (uint32_t b = 16; b < 48; b += 16) {
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
        for (uint32_t r = 0; r < 16; r++) sha_round(s, w, n, r, SHA_K[b + r], true);
    }
    for (uint32_t r = 0; r < 16; r++) sha_round(s, w, n, r, SHA_K[48 + r], true);
}

/* SHA256d of n headers that differ only in the nonce: mid is the first
 * block's state, w0..w2 the second block's first words (big-endian), nonce_be
 * the byte-swapped nonces. Leaves the second SHA-256's state words H[0..8]
 * (the hash bytes are their big-endian serialization) in out. */
SHA_INLINE void sha_sha256d(const uint32_t mid[8], uint32_t w0, uint32_t w1, uint32_t w2, const uint32_t nonce_be[SHA_ILP],
                            uint32_t n, uint32_t out[8][SHA_ILP]) {
    uint32_t s[8][SHA_ILP], w[16][SHA_ILP];
    for (uint32_t j = 0; j < n; j++) {
        for (uint32_t k = 0; k < 8; k++) s[k][j] = mid[k];
        for (uint32_t k = 0; k < 16; k++) w[k][j] = 0;
        w[0][j] = w0, w[1][j] = w1, w[2][j] = w2, w[3][j] = nonce_be[j], w[4][j] = 0x80000000u, w[15][j] = 640;
    }
    sha_rounds64(s, w, n);
    for (uint32_t j = 0; j < n; j++) {
        for (uint32_t k = 0; k < 16; k++) w[k][j] = 0;
        for (uint32_t k = 0; k < 8; k++) w[k][j] = s[k][j] + mid[k];
        w[8][j] = 0x80000000u, w[15][j] = 256;
        for (uint32_t k = 0; k < 8; k++) s[k][j] = SHA_IV[k];
    }
    sha_rounds64(s, w, n);
    for (uint32_t j = 0; j < n; j++)
        for (uint32_t k = 0; k < 8; k++) out[k][j] = s[k][j] + SHA_IV[k];
}

/* One SHA256d as a little-endian 256-bit number (words, 7 most significant).
 * Cold paths only (the best hash, the share rescan): one shared copy. */
__attribute__((noinline)) static void sha_hash1(const uint32_t mid[8], uint32_t w0, uint32_t w1, uint32_t w2, uint32_t nonce, uint32_t out[8]) {
    uint32_t nb[SHA_ILP] = {sha_bswap(nonce), 0}, h[8][SHA_ILP];
    sha_sha256d(mid, w0, w1, w2, nb, 1, h);
    for (uint32_t k = 0; k < 8; k++) out[k] = sha_bswap(h[k][0]);
}

/* ---- the view's spin lock and configuration (lane 0) */
static void sha_lock(mesh_view v) {
    uint8_t *p = mesh_view_ptr(v) + SHA_V_LOCK;
    while (mesh_rt_atom_cas(p, 0, 1) != 0) mesh_rt_nanosleep(64);
    mesh_rt_fence_acq_rel();
}
static void sha_unlock(mesh_view v) {
    mesh_rt_fence_acq_rel();
    mesh_rt_st_release(mesh_view_ptr(v) + SHA_V_LOCK, 0);
}
/* A configuration change begins (locked, gen odd); returns the odd gen for sha_config_end. */
static uint32_t sha_config_begin(mesh_view v) {
    sha_lock(v);
    uint8_t *gp = mesh_view_ptr(v) + SHA_V_GEN;
    uint32_t g = mesh_rt_ld_relaxed(gp) | 1u;
    mesh_rt_st_relaxed(gp, g);
    mesh_rt_fence_acq_rel();
    return g;
}
static void sha_config_end(mesh_view v, uint32_t g) {
    mesh_rt_fence_acq_rel();
    mesh_rt_st_release(mesh_view_ptr(v) + SHA_V_GEN, g + 1);
    sha_unlock(v);
}

/* ---- 256-bit helpers (words k = 7 most significant) */
static uint32_t sha_le_word(uint32_t h) { return sha_bswap(h); }
/* Leading zero bits of a little-endian 256-bit number given as words. */
static uint32_t sha_zbits_of(const uint32_t w[8]) {
    uint32_t z = 0;
    bool done = false;
    for (int k = 7; k >= 0; k--)
        if (!done) {
            if (w[k] == 0) z += 32;
            else { z += (uint32_t)__builtin_clz(w[k]); done = true; }
        }
    return z;
}
static void sha_read8(mesh_view v, uint32_t off, uint32_t w[8]) { for (uint32_t k = 0; k < 8; k++) w[k] = mesh_view_u32(v, off + 4 * k); }
static void sha_write8(mesh_view v, uint32_t off, const uint32_t w[8]) { for (uint32_t k = 0; k < 8; k++) mesh_view_set_u32(v, off + 4 * k, w[k]); }
/* Compares little-endian 256-bit numbers: -1, 0, 1 (the most significant differing word decides). */
static int sha_cmp256(const uint32_t a[8], const uint32_t b[8]) {
    int r = 0;
    for (int k = 0; k < 8; k++)
        if (a[k] != b[k]) r = a[k] < b[k] ? -1 : 1;
    return r;
}

/* The midstate of the header's first 64 bytes (cold: one shared copy). */
__attribute__((noinline)) static void sha_set_midstate(mesh_view v) {
    uint32_t s[8][SHA_ILP], w[16][SHA_ILP];
    for (uint32_t k = 0; k < 8; k++) s[k][0] = SHA_IV[k];
    /* Unchecked reads: init checked view_len >= VIEW_MIN (fixed per image). */
    uint8_t *hp = mesh_view_ptr(v) + SHA_V_HEADER;
    for (uint32_t k = 0; k < 16; k++) w[k][0] = sha_bswap(mesh_rt_rd(hp + 4 * k));
    sha_rounds64(s, w, 1);
    for (uint32_t k = 0; k < 8; k++) mesh_view_set_u32(v, SHA_V_MID + 4 * k, s[k][0] + SHA_IV[k]);
}

static void sha_clear_shares(mesh_view v) {
    mesh_view_set_u32(v, SHA_V_SHARES, 0);
    mesh_view_set_u32(v, SHA_V_FOUND, 0);
    mesh_view_set_u32(v, SHA_V_LAST_NONCE, 0);
    mesh_view_set_u32(v, SHA_V_LAST_TIME, 0);
    mesh_view_set_u32(v, SHA_V_LAST_ZBITS, 0);
    for (uint32_t o = SHA_V_RING; o < SHA_V_RING + SHA_RING * SHA_RING_BYTES; o += 4) mesh_view_set_u32(v, o, 0);
}
static void sha_clear_best(mesh_view v) {
    const uint32_t all[8] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    sha_write8(v, SHA_V_BEST, all);
    mesh_view_set_u32(v, SHA_V_BEST_ZBITS, 0);
    mesh_view_set_u32(v, SHA_V_BEST_NONCE, 0);
    mesh_view_set_u32(v, SHA_V_BEST_TIME, 0);
    mesh_view_set_u32(v, SHA_V_BEST_HI, UINT32_MAX);
}
/* A new search over the same header (base, chunk, time or header changed). */
static void sha_restart(mesh_view v) {
    sha_clear_best(v);
    sha_clear_shares(v);
}
/* The header at V_HEADER changed: its copies, the midstate, a new search. */
static void sha_header_changed(mesh_view v) {
    mesh_view_set_u32(v, SHA_V_VERSION, mesh_view_u32(v, SHA_V_HEADER));
    mesh_view_set_u32(v, SHA_V_TIME, mesh_view_u32(v, SHA_V_HEADER + 68));
    mesh_view_set_u32(v, SHA_V_NBITS, mesh_view_u32(v, SHA_V_HEADER + 72));
    mesh_view_set_u32(v, SHA_V_HEADERS, mesh_view_u32(v, SHA_V_HEADERS) + 1);
    sha_set_midstate(v);
    sha_restart(v);
}
static void sha_target_changed(mesh_view v) {
    uint32_t t[8];
    sha_read8(v, SHA_V_TARGET, t);
    mesh_view_set_u32(v, SHA_V_TARGET_ZBITS, sha_zbits_of(t));
    sha_clear_shares(v);
}
/* Target = 2^(256 - z) - 1 (z leading zero bits). */
static void sha_set_zbits(mesh_view v, uint32_t z) {
    uint32_t lim = 256 - z;
    for (uint32_t k = 0; k < 8; k++) {
        uint32_t lo = 32 * k;
        uint32_t w = lo + 32 <= lim ? UINT32_MAX : lo >= lim ? 0 : (1u << (lim - lo)) - 1;
        mesh_view_set_u32(v, SHA_V_TARGET + 4 * k, w);
    }
    sha_target_changed(v);
}
/* Target from compact bits (mantissa * 256^(exp - 3)); false if negative or overflowing. */
static bool sha_set_nbits(mesh_view v, uint32_t bits) {
    uint32_t exp = bits >> 24, mant = bits & 0x007fffffu;
    if ((bits & 0x00800000u) || exp > 32) return false;
    int32_t pos = (int32_t)exp - 3;
    if (pos < 0) { mant >>= 8 * (uint32_t)(-pos); pos = 0; }
    for (uint32_t k = 0; k < 8; k++) mesh_view_set_u32(v, SHA_V_TARGET + 4 * k, 0);
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t b = (mant >> (8 * i)) & 0xff, at = (uint32_t)pos + i;
        if (at < 32) mesh_view_set_u8(v, SHA_V_TARGET + at, (uint8_t)b);
        else if (b != 0) return false;
    }
    sha_target_changed(v);
    return true;
}
static void sha_set_genesis(mesh_view v) {
    for (uint32_t k = 0; k < 20; k++) mesh_view_set_u32(v, SHA_V_HEADER + 4 * k, SHA_GENESIS[k]);
    mesh_view_set_u32(v, SHA_V_BASE, 0);
    sha_header_changed(v);
}
static void sha_reset_stats(mesh_view v) {
    mesh_view_set_u64(v, SHA_V_HASHES, 0);
    sha_clear_best(v);
    sha_clear_shares(v);
}

static void sha_init(const mesh_ctx *ctx) {
    if (!mesh_ctx_leader(ctx)) return;
    mesh_view v = mesh_ctx_view(ctx);
    /* A view smaller than the layout faults here (bounds-checked), loudly. */
    if (mesh_view_len(v) < SHA_VIEW_MIN) { (void)mesh_view_u32(v, SHA_VIEW_MIN - 4); return; }
    uint32_t g = sha_config_begin(v);
    mesh_view_set_u32(v, SHA_V_MAGIC, SHA_MAGIC);
    mesh_view_set_u32(v, SHA_V_CHUNK_LOG2, mesh_ctx_param(ctx));
    mesh_view_set_u32(v, SHA_V_SYSTEM, mesh_ctx_system(ctx));
    sha_set_genesis(v);
    sha_set_zbits(v, SHA_ZBITS_DEFAULT);
    sha_reset_stats(v);
    sha_config_end(v, g);
}

/* The chunk size the manifest gives (false: out of range). */
static bool sha_chunk_log2(const mesh_ctx *ctx, uint32_t *c) {
    *c = mesh_ctx_param(ctx);
    return *c >= SHA_CHUNK_LOG2_MIN && *c <= SHA_CHUNK_LOG2_MAX;
}
/* Lane 0: if the param changed since the search began (an upgrade), restart
 * the search with the new size (once: rechecked under the lock). */
static void sha_sync_chunk(mesh_view v, uint32_t clog) {
    if (mesh_view_u32(v, SHA_V_CHUNK_LOG2) != clog) {
        uint32_t g = sha_config_begin(v);
        if (mesh_view_u32(v, SHA_V_CHUNK_LOG2) != clog) {
            mesh_view_set_u32(v, SHA_V_CHUNK_LOG2, clog);
            sha_restart(v);
        }
        sha_config_end(v, g);
    }
}

/* One share into the ring (lane 0), if the configuration is still g. */
__attribute__((noinline)) static void sha_record_share(const mesh_ctx *ctx, mesh_view v, uint32_t g, uint32_t nonce, uint32_t ntime, const uint32_t h[8]) {
    sha_lock(v);
    if (mesh_rt_ld_relaxed(mesh_view_ptr(v) + SHA_V_GEN) == g) {
        uint32_t s = mesh_view_u32(v, SHA_V_SHARES);
        uint32_t e = SHA_V_RING + (s % SHA_RING) * SHA_RING_BYTES;
        uint32_t z = sha_zbits_of(h);
        mesh_view_set_u32(v, e, nonce);
        mesh_view_set_u32(v, e + 4, ntime);
        mesh_view_set_u32(v, e + 8, s + 1);
        mesh_view_set_u32(v, e + 12, z);
        sha_write8(v, e + 16, h);
        mesh_view_set_u32(v, SHA_V_SHARES, s + 1);
        mesh_view_set_u32(v, SHA_V_LAST_NONCE, nonce);
        mesh_view_set_u32(v, SHA_V_LAST_TIME, ntime);
        mesh_view_set_u32(v, SHA_V_LAST_ZBITS, z);
        mesh_view_set_u32(v, SHA_V_FOUND, 1);
        mesh_ctx_count(ctx, 1, 1);
    }
    sha_unlock(v);
}

/* The node's shared memory: the configuration warp 0 read (CFG_WORDS words). */
#define SHA_CFG_WORDS 21u

/* One leased chunk (OP_JOB: chunk V_JOB_BASE + job): hash, record. Every thread
 * of the node: warp 0 reads the configuration into shared memory, the node
 * grinds the chunk (nonce thread + threads x i), the best hash and the
 * candidate flag by block reductions, then warp 0 alone records (and rescans
 * the chunk when a thread saw a candidate: rare). */
static uint32_t sha_grind(const mesh_ctx *ctx, uint32_t job) {
    mesh_view v = mesh_ctx_view(ctx);
    uint32_t lane = mesh_ctx_lane(ctx);
    uint8_t *gp = mesh_view_ptr(v) + SHA_V_GEN;
    uint32_t clog;
    if (!sha_chunk_log2(ctx, &clog)) return MESH_R_JOB_FAIL;
    mesh_shared cfg;
    if (!mesh_ctx_shared(ctx, 0, SHA_CFG_WORDS, 4, &cfg)) return MESH_R_JOB_FAIL;
    /* Threads at work: all, or (a chunk smaller than threads x ILP) the first chunk / ILP. */
    uint32_t threads = mesh_ctx_threads(ctx) < (1u << clog) / SHA_ILP ? mesh_ctx_threads(ctx) : (1u << clog) / SHA_ILP;
    if (mesh_ctx_warp(ctx) == 0) {
        if (lane == 0) sha_sync_chunk(v, clog);
        mesh_rt_warp_sync();
        /* Configuration under the gen seqlock. */
        uint32_t g, mid[8], hw[3], base, tgt[8];
        for (;;) {
            uint32_t g1 = 0;
            if (lane == 0) {
                g1 = mesh_rt_ld_acquire(gp);
                while (g1 & 1u) { mesh_rt_nanosleep(256); g1 = mesh_rt_ld_acquire(gp); }
            }
            g1 = mesh_rt_shfl_idx(g1, 0);
            mesh_rt_warp_sync();
            sha_read8(v, SHA_V_MID, mid);
            hw[0] = mesh_view_u32(v, SHA_V_HEADER + 64), hw[1] = mesh_view_u32(v, SHA_V_HEADER + 68), hw[2] = mesh_view_u32(v, SHA_V_HEADER + 72);
            base = mesh_view_u32(v, SHA_V_BASE);
            sha_read8(v, SHA_V_TARGET, tgt);
            mesh_rt_fence_acq_rel();
            mesh_rt_warp_sync();
            uint32_t g2 = lane == 0 ? mesh_rt_ld_acquire(gp) : 0;
            if (mesh_rt_shfl_idx(g2, 0) == g1) { g = g1; break; }
            /* Read across a change: again under the new gen. */
        }
        if (lane == 0) {
            const uint32_t head[5] = {g, base, hw[0], hw[1], hw[2]};
            for (uint32_t i = 0; i < 5; i++) mesh_shared_set_u32(cfg, i, head[i]);
            for (uint32_t i = 0; i < 8; i++) mesh_shared_set_u32(cfg, 5 + i, mid[i]);
            for (uint32_t i = 0; i < 8; i++) mesh_shared_set_u32(cfg, 13 + i, tgt[i]);
        }
    }
    mesh_ctx_block_barrier(ctx);
    uint32_t g = mesh_shared_get_u32(cfg, 0), base = mesh_shared_get_u32(cfg, 1);
    uint32_t hw[3] = {mesh_shared_get_u32(cfg, 2), mesh_shared_get_u32(cfg, 3), mesh_shared_get_u32(cfg, 4)};
    uint32_t mid[8], tgt[8];
    for (uint32_t i = 0; i < 8; i++) mid[i] = mesh_shared_get_u32(cfg, 5 + i), tgt[i] = mesh_shared_get_u32(cfg, 13 + i);
    uint64_t gidx = mesh_view_u64(v, SHA_V_JOB_BASE) + job;
    /* This item's result record: lane 0 fills it. */
    uint32_t rec = SHA_V_JOB_OUT + SHA_JOB_REC * (mesh_ctx_ticket(ctx) % MESH_PROG_RESULTS);
    uint32_t found = 0;
    uint64_t p = gidx << clog;
    uint32_t nonce0 = base + (uint32_t)p;
    uint32_t ntime = hw[1] + (uint32_t)(p >> 32);
    uint32_t w0 = sha_bswap(hw[0]), w1 = sha_bswap(ntime), w2 = sha_bswap(hw[2]);
    uint32_t t7 = tgt[7];

    /* The hot loop: registers only. Thread t takes nonces t + threads x i. */
    uint32_t th = mesh_ctx_thread(ctx);
    uint32_t iters = th < threads ? (1u << clog) / (threads * SHA_ILP) : 0;
    uint32_t bh = UINT32_MAX, bl = UINT32_MAX, bn = 0;
    bool cand = false;
    for (uint32_t k = 0; k < iters; k++) {
        uint32_t n[SHA_ILP], nb[SHA_ILP], h[8][SHA_ILP];
        for (uint32_t j = 0; j < SHA_ILP; j++) {
            n[j] = nonce0 + th + threads * (k * SHA_ILP + j);
            nb[j] = sha_bswap(n[j]);
        }
        sha_sha256d(mid, w0, w1, w2, nb, SHA_ILP, h);
        for (uint32_t j = 0; j < SHA_ILP; j++) {
            uint32_t hi = sha_le_word(h[7][j]), lo = sha_le_word(h[6][j]);
            if (hi < bh || (hi == bh && lo < bl)) { bh = hi; bl = lo; bn = n[j]; }
            cand |= hi <= t7;
        }
    }

    /* The node's best (hi, lo, then the least nonce), the same on every thread; any candidate. */
    uint64_t mine = (uint64_t)bh << 32 | bl;
    uint64_t key = mesh_ctx_block_reduce_u64(ctx, mine, UINT64_MAX, mesh_rt_min_u64);
    bn = mesh_ctx_block_reduce_u32(ctx, mine == key ? bn : UINT32_MAX, UINT32_MAX, mesh_rt_min_u32);
    bool any = mesh_ctx_block_max_u32(ctx, cand ? 1u : 0u) != 0;
    /* Warp 0 alone from here (no block collective follows). */
    if (mesh_ctx_warp(ctx) != 0) return 0;
    uint32_t best[8];
    sha_hash1(mid, w0, w1, w2, bn, best);

    bool stale = false;
    if (lane == 0) {
        sha_lock(v);
        if (mesh_rt_ld_relaxed(gp) == g) {
            uint64_t n = (uint64_t)1 << clog;
            uint64_t old = mesh_view_u64(v, SHA_V_HASHES);
            mesh_view_set_u64(v, SHA_V_HASHES, old + n);
            mesh_ctx_count(ctx, 0, (uint32_t)((old + n) / 1000000 - old / 1000000));
            mesh_ctx_count(ctx, 2, 1);
            mesh_view_set_u32(v, SHA_V_SYSTEM, mesh_ctx_system(ctx));
            uint32_t cur[8];
            sha_read8(v, SHA_V_BEST, cur);
            if (sha_cmp256(best, cur) < 0) {
                sha_write8(v, SHA_V_BEST, best);
                mesh_view_set_u32(v, SHA_V_BEST_ZBITS, sha_zbits_of(best));
                mesh_view_set_u32(v, SHA_V_BEST_NONCE, bn);
                mesh_view_set_u32(v, SHA_V_BEST_TIME, ntime);
                mesh_view_set_u32(v, SHA_V_BEST_HI, best[7]);
            }
        } else {
            stale = true;
            mesh_ctx_count(ctx, 3, 1);
        }
        sha_unlock(v);
    }
    if (mesh_rt_shfl_idx(stale ? 1u : 0u, 0) != 0) return MESH_R_JOB_FAIL;

    /* Shares: a full rescan of the chunk by warp 0 if any thread saw a
     * candidate, one nonce per lane per step. */
    if (any) {
        for (uint32_t k = 0; k < (1u << clog) / 32; k++) {
            uint32_t nonce = nonce0 + lane + 32 * k;
            uint32_t hh[8];
            sha_hash1(mid, w0, w1, w2, nonce, hh);
            uint32_t bits = mesh_rt_ballot(sha_cmp256(hh, tgt) <= 0);
            while (bits != 0) {
                uint32_t src = (uint32_t)__builtin_ctz(bits);
                bits &= bits - 1;
                uint32_t n = mesh_rt_shfl_idx(nonce, src);
                uint32_t sh[8];
                for (uint32_t q = 0; q < 8; q++) sh[q] = mesh_rt_shfl_idx(hh[q], src);
                if (lane == 0) {
                    sha_record_share(ctx, v, g, n, ntime, sh);
                    if (found < SHA_JOB_SHARES) {
                        mesh_view_set_u32(v, rec + 4 + 8 * found, n);
                        mesh_view_set_u32(v, rec + 8 + 8 * found, sha_zbits_of(sh));
                    }
                    found++;
                }
            }
        }
    }
    /* The record, then the result (meshd reads the record once the completion is published). */
    found = mesh_rt_shfl_idx(found, 0);
    if (lane == 0) {
        mesh_view_set_u32(v, rec, found);
        for (uint32_t i = found; i < SHA_JOB_SHARES; i++) {
            mesh_view_set_u32(v, rec + 4 + 8 * i, 0);
            mesh_view_set_u32(v, rec + 8 + 8 * i, 0);
        }
        mesh_view_set_u32(v, rec + 4 + 8 * SHA_JOB_SHARES, ntime);
    }
    return found;
}

/* ---- request window (lane 0) */
typedef struct {
    mesh_view v;
    uint32_t n;
} sha_out;
static void out_b(sha_out *o, uint8_t c) {
    if (o->n < mesh_view_len(o->v)) { mesh_view_set_u8(o->v, o->n, c); o->n++; }
}
static void out_s(sha_out *o, const char *s) { for (; *s; s++) out_b(o, (uint8_t)*s); }
static void out_dec(sha_out *o, uint64_t x) {
    uint64_t d = 1;
    while (x / d >= 10) d *= 10;
    for (;;) {
        out_b(o, (uint8_t)('0' + x / d));
        x %= d;
        if (d == 1) break;
        d /= 10;
    }
}
static void out_hex8(sha_out *o, uint8_t x) {
    static const char hex[] = "0123456789abcdef";
    out_b(o, (uint8_t)hex[(x >> 4) & 15]);
    out_b(o, (uint8_t)hex[x & 15]);
}
static void out_kv(sha_out *o, const char *k, uint64_t x) {
    out_b(o, ' ');
    out_s(o, k);
    out_b(o, '=');
    out_dec(o, x);
}

static int sha_nib(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool sha_is_ws(uint8_t c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == 0; }

/* The next token of the request: start and length (0 at the end). */
static void sha_token(mesh_view r, uint32_t *at, uint32_t *s, uint32_t *len) {
    while (*at < mesh_view_len(r) && sha_is_ws(mesh_view_u8(r, *at))) (*at)++;
    *s = *at;
    while (*at < mesh_view_len(r) && !sha_is_ws(mesh_view_u8(r, *at))) (*at)++;
    *len = *at - *s;
}
static bool sha_tok_is(mesh_view r, uint32_t s, uint32_t len, const char *w) {
    uint32_t n = 0;
    while (w[n]) n++;
    if (len != n) return false;
    for (uint32_t i = 0; i < len; i++)
        if (mesh_view_u8(r, s + i) != (uint8_t)w[i]) return false;
    return true;
}
static bool sha_tok_dec(mesh_view r, uint32_t s, uint32_t len, uint32_t *out) {
    if (len == 0 || len > 10) return false;
    uint64_t x = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = mesh_view_u8(r, s + i);
        if (c < '0' || c > '9') return false;
        x = x * 10 + (uint64_t)(c - '0');
    }
    if (x > UINT32_MAX) return false;
    *out = (uint32_t)x;
    return true;
}
static bool sha_tok_hex32(mesh_view r, uint32_t s, uint32_t len, uint32_t *out) {
    if (len == 0 || len > 8) return false;
    uint32_t x = 0;
    for (uint32_t i = 0; i < len; i++) {
        int d = sha_nib(mesh_view_u8(r, s + i));
        if (d < 0) return false;
        x = x << 4 | (uint32_t)d;
    }
    *out = x;
    return true;
}
/* Hex bytes of the token into the view at dst (n bytes; rev: reversed, display order). */
static bool sha_tok_bytes(mesh_view r, uint32_t s, uint32_t len, mesh_view v, uint32_t dst, uint32_t n, bool rev) {
    if (len != 2 * n) return false;
    for (uint32_t i = 0; i < n; i++) {
        int a = sha_nib(mesh_view_u8(r, s + 2 * i)), b = sha_nib(mesh_view_u8(r, s + 2 * i + 1));
        if (a < 0 || b < 0) return false;
        mesh_view_set_u8(v, dst + (rev ? n - 1 - i : i), (uint8_t)(a << 4 | b));
    }
    return true;
}

#define SHA_T_NONE 0u
#define SHA_T_ZBITS 1u
#define SHA_T_NBITS 2u
#define SHA_T_BYTES 3u

/* Parses the whole request (into V_STAGE), then applies it as one change. */
__attribute__((noinline)) static uint32_t sha_request(const mesh_ctx *ctx) {
    mesh_view r = mesh_ctx_request(ctx), v = mesh_ctx_view(ctx);
    sha_out o = {mesh_ctx_response(ctx), 0};
    bool hdr = false, has_base = false, has_time = false, genesis = false, reset = false, status = false;
    uint32_t tk = SHA_T_NONE, tv = 0, base = 0, time = 0;
    const char *err = NULL;
    uint8_t first = mesh_view_u8(r, 0);
    if ((mesh_view_len(r) == 80 || mesh_view_len(r) == 112) && !(first >= 'a' && first <= 'z')) {
        for (uint32_t i = 0; i < mesh_view_len(r); i++) mesh_view_set_u8(v, SHA_V_STAGE + i, mesh_view_u8(r, i));
        hdr = true;
        if (mesh_view_len(r) == 112) tk = SHA_T_BYTES;
    } else {
        uint32_t at = 0;
        for (;;) {
            uint32_t s, n, as, an;
            sha_token(r, &at, &s, &n);
            if (n == 0) break;
            if (sha_tok_is(r, s, n, "header")) {
                sha_token(r, &at, &as, &an);
                if (!sha_tok_bytes(r, as, an, v, SHA_V_STAGE, 80, false)) { err = "header: 160 hex digits"; break; }
                hdr = true;
            } else if (sha_tok_is(r, s, n, "target")) {
                sha_token(r, &at, &as, &an);
                if (!sha_tok_bytes(r, as, an, v, SHA_V_STAGE + 80, 32, true)) { err = "target: 64 hex digits"; break; }
                tk = SHA_T_BYTES;
            } else if (sha_tok_is(r, s, n, "zbits")) {
                uint32_t z;
                sha_token(r, &at, &as, &an);
                if (sha_tok_dec(r, as, an, &z) && z >= 1 && z <= 255) { tk = SHA_T_ZBITS; tv = z; }
                else { err = "zbits: 1..=255"; break; }
            } else if (sha_tok_is(r, s, n, "nbits")) {
                uint32_t b;
                sha_token(r, &at, &as, &an);
                if (sha_tok_hex32(r, as, an, &b)) { tk = SHA_T_NBITS; tv = b; }
                else { err = "nbits: 8 hex digits"; break; }
            } else if (sha_tok_is(r, s, n, "base")) {
                sha_token(r, &at, &as, &an);
                if (sha_tok_dec(r, as, an, &base)) has_base = true;
                else { err = "base: a u32"; break; }
            } else if (sha_tok_is(r, s, n, "time")) {
                sha_token(r, &at, &as, &an);
                if (sha_tok_dec(r, as, an, &time)) has_time = true;
                else { err = "time: a u32"; break; }
            } else if (sha_tok_is(r, s, n, "chunk")) {
                err = "chunk: fixed by the manifest's param (one chunk size on every member of a cluster)";
                break;
            } else if (sha_tok_is(r, s, n, "genesis")) {
                genesis = true;
            } else if (sha_tok_is(r, s, n, "reset")) {
                reset = true;
            } else if (sha_tok_is(r, s, n, "status")) {
                status = true;
            } else {
                err = "unknown word (header target zbits nbits base time genesis reset status)";
                break;
            }
        }
    }
    if (err) {
        out_s(&o, "err ");
        out_s(&o, err);
        out_b(&o, '\n');
        return o.n;
    }
    uint32_t clog;
    if (!sha_chunk_log2(ctx, &clog)) {
        out_s(&o, "err the manifest's param (chunk_log2) is outside 6..=24\n");
        return o.n;
    }
    sha_sync_chunk(v, clog);
    /* A pure `status` changes nothing (and keeps gen: no in-flight chunk goes stale). */
    bool change = genesis || hdr || has_time || has_base || tk != SHA_T_NONE || reset;
    bool ok = true;
    if (change) {
        uint32_t g = sha_config_begin(v);
        if (genesis) sha_set_genesis(v);
        if (hdr)
            for (uint32_t i = 0; i < 80; i++) mesh_view_set_u8(v, SHA_V_HEADER + i, mesh_view_u8(v, SHA_V_STAGE + i));
        if (has_time) mesh_view_set_u32(v, SHA_V_HEADER + 68, time);
        if (has_base) mesh_view_set_u32(v, SHA_V_BASE, base);
        if (hdr || has_time) sha_header_changed(v);
        else if (has_base) sha_restart(v);
        if (tk == SHA_T_ZBITS) sha_set_zbits(v, tv);
        else if (tk == SHA_T_NBITS) ok = sha_set_nbits(v, tv);
        else if (tk == SHA_T_BYTES) {
            for (uint32_t i = 0; i < 32; i++) mesh_view_set_u8(v, SHA_V_TARGET + i, mesh_view_u8(v, SHA_V_STAGE + 80 + i));
            sha_target_changed(v);
        }
        if (reset) sha_reset_stats(v);
        sha_config_end(v, g);
    }
    if (!ok) {
        out_s(&o, "err nbits: negative or overflowing target\n");
        return o.n;
    }
    out_s(&o, "ok");
    out_kv(&o, "gen", mesh_rt_ld_relaxed(mesh_view_ptr(v) + SHA_V_GEN));
    out_kv(&o, "zbits", mesh_view_u32(v, SHA_V_TARGET_ZBITS));
    out_kv(&o, "base", mesh_view_u32(v, SHA_V_BASE));
    out_kv(&o, "chunk_log2", mesh_view_u32(v, SHA_V_CHUNK_LOG2));
    out_kv(&o, "time", mesh_view_u32(v, SHA_V_TIME));
    if (status) {
        out_kv(&o, "hashes", mesh_view_u64(v, SHA_V_HASHES));
        out_kv(&o, "shares", mesh_view_u32(v, SHA_V_SHARES));
        out_kv(&o, "best_zbits", mesh_view_u32(v, SHA_V_BEST_ZBITS));
        out_kv(&o, "best_nonce", mesh_view_u32(v, SHA_V_BEST_NONCE));
        out_s(&o, " best=");
        for (int i = 31; i >= 0; i--) out_hex8(&o, mesh_view_u8(v, SHA_V_BEST + (uint32_t)i));
    }
    out_b(&o, '\n');
    return o.n;
}

static uint32_t sha_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    if (op == MESH_OP_JOB) return sha_grind(ctx, arg);
    /* Everything but the grind runs on warp 0. */
    if (mesh_ctx_warp(ctx) != 0) return MESH_R_NONE;
    if (op == MESH_OP_REQ) {
        uint32_t n = mesh_ctx_leader(ctx) ? sha_request(ctx) : 0;
        n = mesh_rt_shfl_idx(n, 0);
        mesh_ctx_respond(ctx, n);
        return n;
    }
    if (op >= SHA_OP_ZBITS && op <= SHA_OP_TIME) {
        if (!mesh_ctx_leader(ctx)) return MESH_R_NONE;
        mesh_view v = mesh_ctx_view(ctx);
        uint32_t c;
        if (sha_chunk_log2(ctx, &c)) sha_sync_chunk(v, c);
        bool ok = false;
        uint32_t g;
        switch (op) {
        case SHA_OP_ZBITS:
            if (arg >= 1 && arg <= 255) { g = sha_config_begin(v); sha_set_zbits(v, arg); sha_config_end(v, g); ok = true; }
            break;
        case SHA_OP_RESET: g = sha_config_begin(v); sha_reset_stats(v); sha_config_end(v, g); ok = true; break;
        case SHA_OP_NBITS: g = sha_config_begin(v); ok = sha_set_nbits(v, arg); sha_config_end(v, g); break;
        case SHA_OP_GENESIS:
            if (arg <= 255) {
                g = sha_config_begin(v);
                sha_set_genesis(v);
                if (arg == 0) ok = sha_set_nbits(v, mesh_view_u32(v, SHA_V_NBITS));
                else { sha_set_zbits(v, arg); ok = true; }
                sha_config_end(v, g);
            }
            break;
        case SHA_OP_BASE: g = sha_config_begin(v); mesh_view_set_u32(v, SHA_V_BASE, arg); sha_restart(v); sha_config_end(v, g); ok = true; break;
        case SHA_OP_TIME: g = sha_config_begin(v); mesh_view_set_u32(v, SHA_V_HEADER + 68, arg); sha_header_changed(v); sha_config_end(v, g); ok = true; break;
        default: break; /* OP_CHUNK: refused */
        }
        return ok ? MESH_R_NONE : MESH_R_BAD_OP;
    }
    return MESH_R_BAD_OP;
}

MESH_PROGRAM(sha_init, sha_task);
