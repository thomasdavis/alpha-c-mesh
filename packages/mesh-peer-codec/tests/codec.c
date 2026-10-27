/* peer.rs: join_frame_and_checks, fleet_frames_round_trip, names_on_the_wire. */
#include "mesh/abi/backend.h"
#include "mesh/abi/group_state.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/abi/reconcile.h"
#include "mesh/check.h"
#include "mesh/peer_codec.h"
#include "mesh/text.h"

#include <stdlib.h>

static mpk_caps caps(void) {
    mpk_caps c = {.cc = 121, .sms = 48, .sms_total = 48, .blocks_per_sm = 24, .gpu = "NVIDIA GB10", .arch = "aarch64", .host = "dgx-00", .max_threads = 1024, .max_shared = 101376};
    return c;
}

static bool join_eq(const mpk_join *a, const mpk_join *b) { return a->system == b->system && a->version == b->version && a->systems == b->systems && mpk_caps_eq(&a->caps, &b->caps); }

static bool rejects(const mpk_join *j, uint32_t me, uint32_t n, bool has, uint32_t exp, const char *needle) {
    char *err = NULL;
    if (mpk_join_check(j, me, n, has, exp, &err)) return false;
    bool ok = !needle || mt_contains(err, needle);
    free(err);
    return ok;
}

int main(void) {
    /* join_frame_and_checks */
    mpk_join j = {.system = 2, .version = MESH_PEER_VERSION, .systems = 3, .caps = caps()};
    j.caps.cc = 86, j.caps.blocks_per_sm = 16;
    strcpy(j.caps.gpu, "NVIDIA GeForce RTX 3060");
    strcpy(j.caps.arch, "x86_64");
    mpk_join mj = j;
    mj.caps = caps();
    mj.caps.cc = 0, mj.caps.blocks_per_sm = 0, mj.caps.backend = mesh_backend_word(MESH_BACKEND_METAL, 9);
    strcpy(mj.caps.gpu, "Apple M3 Ultra");
    mpw_bytes f = mpk_join_encode(&mj);
    mpk_join back = mpk_join_decode(f.b + 1);
    CHECK(join_eq(&back, &mj));
    mpw_bytes_free(&f);
    f = mpk_join_encode(&j);
    CHECK(f.b[0] == MESH_F_JOIN && f.n == 1 + MESH_JOIN_BYTES);
    back = mpk_join_decode(f.b + 1);
    CHECK(join_eq(&back, &j));
    mpw_bytes_free(&f);
    char *err = NULL;
    CHECK(mpk_join_check(&j, 0, 3, true, 2, &err));
    CHECK(rejects(&j, 0, 3, true, 1, NULL));
    mpk_join j0 = j;
    j0.system = 0;
    CHECK(mpk_join_check(&j0, 1, 3, false, 0, &err));
    CHECK(rejects(&j, 1, 3, false, 0, NULL));
    CHECK(rejects(&j, 0, 4, true, 2, "cluster of 3"));
    mpk_join j5 = j;
    j5.version = 5;
    CHECK(rejects(&j5, 0, 3, true, 2, "version 5"));

    /* fleet_frames_round_trip */
    mst_outcome moves[] = {{"pipehash", 41, true, 3100}, {"src", 42, false, 0}};
    mpk_beat_status st = {.has_leader = true, .leader = 2, .applied = 9, .status = MESH_RS_FAILED, .up = 0xb, .group_epoch = 4, .group_state = MESH_GL_PAUSED,
                          .reason = "load hello: jit: boom", .moves = moves, .nmoves = 2};
    mfc_msg m = {.kind = MFC_BEAT, .term = 7, .leader = true, .commit = {6, 9}, .acc = {7, 10}};
    f = mpk_beat_frame(&m, &st);
    size_t n = strlen(st.reason);
    CHECK(f.b[0] == MESH_F_BEAT && f.n == 1 + MESH_BEAT_HEAD + n + 4 + 2 * MESH_BEAT_MOVE_BYTES);
    mfc_msg m2;
    mpk_beat_status st2;
    CHECK(mpk_beat_decode(f.b + 1, f.b + 1 + MESH_BEAT_HEAD, n, f.b + 1 + MESH_BEAT_HEAD + n + 4, 2 * MESH_BEAT_MOVE_BYTES, &m2, &st2, &err));
    CHECK(m2.term == 7 && m2.leader && m2.commit.term == 6 && m2.commit.version == 9 && m2.acc.term == 7 && m2.acc.version == 10);
    CHECK(mpk_beat_status_eq(&st2, &st));
    mpk_beat_status_free(&st2);
    mpw_bytes_free(&f);
    mpk_hello spec = {.group = "train", .epoch = 3, .cycle = 5, .mask = 0xd, .order = {0, 2, 3, 0xff, 0xff, 0xff, 0xff, 0xff}, .hash = 0xfeed};
    mpw_bytes h = mpk_hello_frame(2, &spec);
    CHECK(h.n == 1 + MESH_HELLO_BYTES);
    uint32_t sys, ver, r;
    mpk_hello hb;
    CHECK(mpk_hello_decode(h.b + 1, &sys, &ver, &hb, &err) && sys == 2 && ver == MESH_PEER_VERSION && mpk_hello_eq(&hb, &spec));
    CHECK(mpk_hello_rank(&spec, 3, &r) && r == 2 && !mpk_hello_rank(&spec, 1, &r) && mpk_hello_n(&spec) == 3);
    mpw_bytes_free(&h);
    mfc_msg msgs[] = {{.kind = MFC_VOTE_REQ, .pre = true, .term = 3, .acc = {2, 8}}, {.kind = MFC_VOTE, .pre = false, .term = 3, .granted = true},
                      {.kind = MFC_STATE_ACK, .term = 3, .ver = {3, 9}}, {.kind = MFC_COMMIT, .term = 3, .ver = {3, 9}}};
    for (int i = 0; i < 4; i++) {
        f = mpk_msg_frame(&msgs[i]);
        size_t len = f.b[0] == MESH_F_VOTE_REQ ? MESH_VOTE_REQ_BYTES : f.b[0] == MESH_F_VOTE ? MESH_VOTE_BYTES : MESH_ACK_BYTES;
        CHECK(f.n == 1 + len);
        mfc_msg d = f.b[0] == MESH_F_VOTE_REQ ? mpk_vote_req_decode(f.b + 1) : f.b[0] == MESH_F_VOTE ? mpk_vote_decode(f.b + 1) : mpk_ack_decode(f.b[0], f.b + 1);
        CHECK(d.kind == msgs[i].kind && d.term == msgs[i].term && d.pre == msgs[i].pre && d.granted == msgs[i].granted && d.acc.version == msgs[i].acc.version &&
              d.ver.version == msgs[i].ver.version);
        mpw_bytes_free(&f);
    }
    mfc_msg sm = {.kind = MFC_STATE, .term = 1};
    char *empty = mfd_encode(&sm.doc);
    f = mpk_msg_frame(&sm);
    CHECK(f.n == 1 + MESH_STATE_HEAD + strlen(empty));
    free(empty);
    mpw_bytes_free(&f);
    return mesh_check_done();
}
