/* peer.rs: Caps, Join, BeatStatus, frame, msg_frame, beat_frame, moves_bytes, moves_decode,
 * beat_decode, hello_frame, hello_decode; shared.rs HelloSpec. */
#include "mesh/peer_codec.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <string.h>

static void put64(mpw_bytes *o, uint64_t v) {
    mpw_put32(o, (uint32_t)v);
    mpw_put32(o, (uint32_t)(v >> 32));
}

static void str_field(mpw_bytes *o, const char *s, size_t n) {
    uint8_t b[64] = {0};
    size_t k = strlen(s);
    if (k > n - 1) k = n - 1;
    memcpy(b, s, k);
    mpw_put(o, b, n);
}

static void cstr(char *out, const uint8_t *b, size_t n) {
    size_t k = 0;
    while (k < n && b[k]) k++;
    memcpy(out, b, k);
    out[k] = 0;
}

mpw_bytes mpk_join_encode(const mpk_join *j) {
    mpw_bytes o = {0};
    uint8_t t = MESH_F_JOIN;
    const mpk_caps *c = &j->caps;
    mpw_put(&o, &t, 1);
    uint32_t v[] = {j->system, j->version, j->systems, c->cc, c->sms, c->sms_total, c->blocks_per_sm, c->backend};
    for (int i = 0; i < 8; i++) mpw_put32(&o, v[i]);
    str_field(&o, c->gpu, 32);
    str_field(&o, c->arch, 16);
    str_field(&o, c->host, 32);
    mpw_put32(&o, c->max_threads);
    mpw_put32(&o, c->max_shared);
    return o;
}

mpk_join mpk_join_decode(const uint8_t *b) {
    mpk_join j = {mpw_u32_at(b, 0), mpw_u32_at(b, 4), mpw_u32_at(b, 8), {0}};
    mpk_caps *c = &j.caps;
    c->cc = mpw_u32_at(b, 12), c->sms = mpw_u32_at(b, 16), c->sms_total = mpw_u32_at(b, 20), c->blocks_per_sm = mpw_u32_at(b, 24);
    c->backend = mpw_u32_at(b, 28);
    cstr(c->gpu, b + 32, 32);
    cstr(c->arch, b + 64, 16);
    cstr(c->host, b + 80, 32);
    c->max_threads = mpw_u32_at(b, 112), c->max_shared = mpw_u32_at(b, 116);
    return j;
}

bool mpk_join_check(const mpk_join *j, uint32_t me, uint32_t n, bool has_expect, uint32_t expect, char **err) {
    if (j->version != MESH_PEER_VERSION) {
        *err = mt_fmt("system %u runs peer-link version %u, we run %u", j->system, j->version, (unsigned)MESH_PEER_VERSION);
        return false;
    }
    if (j->systems != n) {
        *err = mt_fmt("system %u is in a cluster of %u, we are in one of %u", j->system, j->systems, n);
        return false;
    }
    if (j->system >= n || j->system == me) {
        *err = mt_fmt("a JOIN from system %u (we are system %u of %u)", j->system, me, n);
        return false;
    }
    if (has_expect && j->system != expect) {
        *err = mt_fmt("dialed system %u, it says it is system %u", expect, j->system);
        return false;
    }
    if (!has_expect && j->system >= me) {
        *err = mt_fmt("system %u dialed us (system %u); only lower systems dial", j->system, me);
        return false;
    }
    return true;
}

bool mpk_caps_eq(const mpk_caps *a, const mpk_caps *b) {
    return a->cc == b->cc && a->sms == b->sms && a->sms_total == b->sms_total && a->blocks_per_sm == b->blocks_per_sm && !strcmp(a->gpu, b->gpu) &&
           !strcmp(a->arch, b->arch) && !strcmp(a->host, b->host) && a->backend == b->backend && a->max_threads == b->max_threads && a->max_shared == b->max_shared;
}

void mpk_beat_status_free(mpk_beat_status *s) {
    free(s->reason);
    for (size_t i = 0; i < s->nmoves; i++) free(s->moves[i].name);
    free(s->moves);
    *s = (mpk_beat_status){0};
}

mpk_beat_status mpk_beat_status_clone(const mpk_beat_status *s) {
    mpk_beat_status c = *s;
    c.reason = mt_strdup(s->reason ? s->reason : "");
    c.moves = mt_alloc((s->nmoves ? s->nmoves : 1) * sizeof *c.moves);
    for (size_t i = 0; i < s->nmoves; i++) c.moves[i] = (mst_outcome){mt_strdup(s->moves[i].name), s->moves[i].version, s->moves[i].ok, s->moves[i].gap_us};
    return c;
}

bool mpk_beat_status_eq(const mpk_beat_status *a, const mpk_beat_status *b) {
    if (a->has_leader != b->has_leader || (a->has_leader && a->leader != b->leader) || a->applied != b->applied || a->status != b->status || a->up != b->up ||
        a->group_epoch != b->group_epoch || a->group_state != b->group_state || strcmp(a->reason ? a->reason : "", b->reason ? b->reason : "") || a->nmoves != b->nmoves)
        return false;
    for (size_t i = 0; i < a->nmoves; i++)
        if (strcmp(a->moves[i].name, b->moves[i].name) || a->moves[i].version != b->moves[i].version || a->moves[i].ok != b->moves[i].ok || a->moves[i].gap_us != b->moves[i].gap_us)
            return false;
    return true;
}

mpw_bytes mpk_msg_frame(const mfc_msg *m) {
    mpw_bytes o = {0};
    uint8_t t;
    switch (m->kind) {
    case MFC_VOTE_REQ:
        t = MESH_F_VOTE_REQ;
        mpw_put(&o, &t, 1);
        mpw_put32(&o, m->pre);
        put64(&o, m->term);
        put64(&o, m->acc.term);
        put64(&o, m->acc.version);
        break;
    case MFC_VOTE:
        t = MESH_F_VOTE;
        mpw_put(&o, &t, 1);
        mpw_put32(&o, m->pre);
        put64(&o, m->term);
        mpw_put32(&o, m->granted);
        break;
    case MFC_STATE: {
        t = MESH_F_STATE;
        mpw_put(&o, &t, 1);
        char *doc = mfd_encode(&m->doc);
        put64(&o, m->term);
        mpw_put32(&o, (uint32_t)strlen(doc));
        mpw_put(&o, doc, strlen(doc));
        free(doc);
        break;
    }
    case MFC_STATE_ACK:
    case MFC_COMMIT:
        t = m->kind == MFC_STATE_ACK ? MESH_F_STATE_ACK : MESH_F_COMMIT;
        mpw_put(&o, &t, 1);
        put64(&o, m->term);
        put64(&o, m->ver.term);
        put64(&o, m->ver.version);
        break;
    case MFC_BEAT: /* beats carry the status: mpk_beat_frame */
        break;
    }
    return o;
}

mpw_bytes mpk_beat_frame(const mfc_msg *m, const mpk_beat_status *st) {
    mpw_bytes o = {0};
    uint8_t t = MESH_F_BEAT;
    const char *reason = st->reason ? st->reason : "";
    size_t rn = strlen(reason);
    if (rn > MESH_BEAT_REASON_MAX) rn = MESH_BEAT_REASON_MAX;
    mpw_put(&o, &t, 1);
    put64(&o, m->term);
    mpw_put32(&o, m->leader ? MESH_BEAT_LEADER : 0);
    mpw_put32(&o, st->has_leader ? st->leader : MESH_PEER_NONE);
    put64(&o, m->commit.term);
    put64(&o, m->commit.version);
    put64(&o, m->acc.term);
    put64(&o, m->acc.version);
    put64(&o, st->applied);
    mpw_put32(&o, st->status);
    mpw_put32(&o, st->up);
    mpw_put32(&o, st->group_epoch);
    mpw_put32(&o, st->group_state);
    mpw_put32(&o, (uint32_t)rn);
    mpw_put(&o, reason, rn);
    /* F_BEAT's tail: u32 count, then per move outcome its name, version, ok and gap. */
    size_t k = st->nmoves < MESH_BEAT_MOVES_MAX ? st->nmoves : MESH_BEAT_MOVES_MAX;
    mpw_put32(&o, (uint32_t)k);
    for (size_t i = 0; i < k; i++) {
        uint8_t name[MESH_NAME_BYTES];
        mpw_name16(st->moves[i].name, name);
        mpw_put(&o, name, sizeof name);
        put64(&o, st->moves[i].version);
        mpw_put32(&o, st->moves[i].ok);
        mpw_put32(&o, st->moves[i].gap_us);
    }
    return o;
}

bool mpk_beat_decode(const uint8_t *h, const uint8_t *reason, size_t nreason, const uint8_t *moves, size_t nmoves, mfc_msg *msg, mpk_beat_status *st, char **err) {
    uint32_t l = mpw_u32_at(h, 12);
    *msg = (mfc_msg){.kind = MFC_BEAT, .term = mpw_u64_at(h, 0), .leader = (mpw_u32_at(h, 8) & MESH_BEAT_LEADER) != 0, .commit = {mpw_u64_at(h, 16), mpw_u64_at(h, 24)},
                     .acc = {mpw_u64_at(h, 32), mpw_u64_at(h, 40)}};
    mpk_beat_status s = {.has_leader = l != MESH_PEER_NONE, .leader = l != MESH_PEER_NONE ? l : 0, .applied = mpw_u64_at(h, 48), .status = mpw_u32_at(h, 56),
                         .up = mpw_u32_at(h, 60), .group_epoch = mpw_u32_at(h, 64), .group_state = mpw_u32_at(h, 68)};
    /* String::from_utf8_lossy: the bytes as they are (the fleet's reasons are ASCII). */
    s.reason = mt_strndup((const char *)reason, nreason);
    size_t k = nmoves / MESH_BEAT_MOVE_BYTES;
    s.moves = mt_alloc((k ? k : 1) * sizeof *s.moves);
    for (size_t i = 0; i < k; i++) {
        const uint8_t *c = moves + i * MESH_BEAT_MOVE_BYTES;
        char name[MESH_NAME_BYTES + 1];
        if (!mpw_name_of(c, MESH_NAME_BYTES, name, err)) {
            mpk_beat_status_free(&s);
            return false;
        }
        s.moves[s.nmoves++] = (mst_outcome){mt_strdup(name), mpw_u64_at(c, MESH_NAME_BYTES), mpw_u32_at(c, MESH_NAME_BYTES + 8) != 0, mpw_u32_at(c, MESH_NAME_BYTES + 12)};
    }
    *st = s;
    return true;
}

mfc_msg mpk_vote_req_decode(const uint8_t *b) {
    return (mfc_msg){.kind = MFC_VOTE_REQ, .pre = mpw_u32_at(b, 0) != 0, .term = mpw_u64_at(b, 4), .acc = {mpw_u64_at(b, 12), mpw_u64_at(b, 20)}};
}

mfc_msg mpk_vote_decode(const uint8_t *b) { return (mfc_msg){.kind = MFC_VOTE, .pre = mpw_u32_at(b, 0) != 0, .term = mpw_u64_at(b, 4), .granted = mpw_u32_at(b, 12) != 0}; }

mfc_msg mpk_ack_decode(uint8_t type, const uint8_t *b) {
    return (mfc_msg){.kind = type == MESH_F_STATE_ACK ? MFC_STATE_ACK : MFC_COMMIT, .term = mpw_u64_at(b, 0), .ver = {mpw_u64_at(b, 8), mpw_u64_at(b, 16)}};
}

bool mpk_hello_rank(const mpk_hello *h, uint32_t k, uint32_t *rank) {
    for (uint32_t r = 0; r < MESH_MAX_SYSTEMS; r++)
        if (h->order[r] == k) {
            if (rank) *rank = r;
            return true;
        }
    return false;
}

uint32_t mpk_hello_n(const mpk_hello *h) {
    uint32_t n = 0;
    for (uint32_t m = h->mask; m; m &= m - 1) n++;
    return n;
}

bool mpk_hello_eq(const mpk_hello *a, const mpk_hello *b) {
    return !strcmp(a->group, b->group) && a->epoch == b->epoch && a->cycle == b->cycle && a->mask == b->mask && !memcmp(a->order, b->order, sizeof a->order) &&
           a->hash == b->hash;
}

mpw_bytes mpk_hello_frame(uint32_t me, const mpk_hello *h) {
    mpw_bytes o = {0};
    uint8_t t = MESH_F_HELLO, name[MESH_NAME_BYTES];
    mpw_put(&o, &t, 1);
    mpw_put32(&o, me);
    mpw_put32(&o, MESH_PEER_VERSION);
    mpw_name16(h->group, name);
    mpw_put(&o, name, sizeof name);
    mpw_put32(&o, h->epoch);
    mpw_put32(&o, h->cycle);
    mpw_put32(&o, h->mask);
    mpw_put(&o, h->order, MESH_MAX_SYSTEMS);
    put64(&o, h->hash);
    return o;
}

bool mpk_hello_decode(const uint8_t *b, uint32_t *system, uint32_t *version, mpk_hello *out, char **err) {
    size_t at = 8 + MESH_NAME_BYTES;
    mpk_hello h = {.epoch = mpw_u32_at(b, at), .cycle = mpw_u32_at(b, at + 4), .mask = mpw_u32_at(b, at + 8), .hash = mpw_u64_at(b, at + 12 + MESH_MAX_SYSTEMS)};
    if (!mpw_name_of(b + 8, MESH_NAME_BYTES, h.group, err)) return false;
    memcpy(h.order, b + at + 12, MESH_MAX_SYSTEMS);
    *system = mpw_u32_at(b, 0);
    *version = mpw_u32_at(b, 4);
    *out = h;
    return true;
}
