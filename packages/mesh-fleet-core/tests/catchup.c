/* fleet.rs's catch-up tests (review finding 1) over a network delivered by hand, and review finding
 * 4: nothing leaves without its persist. */
#include "mesh/check.h"
#include "mesh/fleet_core.h"
#include "mesh/mt.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t from, to;
    mfc_msg m;
} qitem;

typedef struct {
    mfc_core *c[5];
    uint32_t n;
    qitem *q;
    size_t qh, qn, qcap;
    bool down[5];
    uint64_t now;
    mfc_id high[5];
} net_t;

static void enqueue(net_t *n, uint32_t from, uint32_t to, mfc_msg m) {
    if (n->qn == n->qcap) {
        n->qcap = n->qcap ? n->qcap * 2 : 64;
        n->q = mt_realloc(n->q, n->qcap * sizeof *n->q);
    }
    n->q[n->qn++] = (qitem){from, to, m};
}

static void out(net_t *n, uint32_t k, mfc_outs o) {
    for (size_t i = 0; i < o.n; i++)
        if (o.v[i].kind == MFC_OUT_SEND) {
            enqueue(n, k, o.v[i].to, o.v[i].msg);
            o.v[i].kind = MFC_OUT_LOG; /* moved */
        }
    mfc_outs_free(&o);
    mfc_id id = mfc_doc_id(mfc_committed(n->c[k]));
    CHECK(mfc_id_cmp(id, n->high[k]) >= 0);
    n->high[k] = id;
}

static void net_init(net_t *n, uint32_t count) {
    memset(n, 0, sizeof *n);
    n->n = count;
    n->now = 1;
    for (uint32_t k = 0; k < count; k++) n->c[k] = mfc_new((mfc_config){k, count, 3000, 7 + k}, (mfp_persist){0}, 1);
    for (uint32_t a = 0; a < count; a++)
        for (uint32_t b = 0; b < count; b++)
            if (a != b) out(n, a, mfc_link(n->c[a], b, true, 1));
}

static void net_free(net_t *n) {
    for (uint32_t k = 0; k < n->n; k++) mfc_free(n->c[k]);
    for (size_t i = n->qh; i < n->qn; i++) mfc_msg_free(&n->q[i].m);
    free(n->q);
}

typedef bool (*keep_fn)(uint32_t from, uint32_t to, const mfc_msg *m);

static bool all_keep(uint32_t f, uint32_t t, const mfc_msg *m) { return (void)f, (void)t, (void)m, true; }
static bool none_keep(uint32_t f, uint32_t t, const mfc_msg *m) { return (void)f, (void)t, (void)m, false; }

/* Delivers everything queued; keep false drops it. */
static void run(net_t *n, keep_fn keep) {
    while (n->qh < n->qn) {
        qitem it = n->q[n->qh++];
        if (n->down[it.from] || n->down[it.to] || !keep(it.from, it.to, &it.m)) {
            mfc_msg_free(&it.m);
            continue;
        }
        out(n, it.to, mfc_recv(n->c[it.to], it.from, it.m, n->now));
    }
}

static void crash(net_t *n, uint32_t k) {
    n->down[k] = true;
    for (uint32_t j = 0; j < n->n; j++)
        if (j != k && !n->down[j]) out(n, j, mfc_link(n->c[j], k, false, n->now));
}

static void elect(net_t *n, uint32_t k) {
    n->now += 5000;
    out(n, k, mfc_tick(n->c[k], n->now));
    run(n, all_keep);
    CHECK(mfc_role_of(n->c[k]) == MFC_LEADER);
}

static void beat(net_t *n, uint32_t k, uint32_t to) { enqueue(n, k, to, mfc_beat(n->c[k])); }

static uint64_t propose(net_t *n, uint32_t k, const char *tag) {
    mfd_doc d = mfd_doc_clone(mfc_committed(n->c[k]));
    mt_strs w = {0};
    mt_push(&w, "local");
    mfd_place_put(&d, tag, w);
    uint64_t v = 0;
    mfc_outs o;
    char *err = NULL;
    CHECK(mfc_propose(n->c[k], d, n->now, &v, &o, &err));
    out(n, k, o);
    return v;
}

/* peer.rs `lead`: the term's first document (the accepted one re-stamped). */
static uint64_t first(net_t *n, uint32_t k) {
    CHECK(!mfc_term_committed(n->c[k]));
    mfd_doc d = {0};
    mfc_first_doc(n->c[k], &d);
    uint64_t v = 0;
    mfc_outs o;
    char *err = NULL;
    CHECK(mfc_propose(n->c[k], d, n->now, &v, &o, &err));
    out(n, k, o);
    return v;
}

static bool not_commit_0_to_1(uint32_t f, uint32_t t, const mfc_msg *m) { return !(f == 0 && t == 1 && m->kind == MFC_COMMIT); }
static bool not_to_2(uint32_t f, uint32_t t, const mfc_msg *m) { return (void)f, (void)m, t != 2; }
static bool n5_filter(uint32_t f, uint32_t t, const mfc_msg *m) {
    return !(f == 0 && (t == 3 || t == 4) && m->kind == MFC_STATE) && !(f == 0 && t != 2 && m->kind == MFC_COMMIT);
}

/* N = 3: a new leader whose committed document is behind must not catch a follower "up" to it. */
/* fleet.rs catch_up_never_regresses_a_committed_document */
static void never_regresses(void) {
    net_t n;
    net_init(&n, 3);
    elect(&n, 0);
    first(&n, 0);
    run(&n, all_keep);
    propose(&n, 0, "a");
    run(&n, all_keep);
    uint64_t v2 = propose(&n, 0, "x");
    run(&n, not_commit_0_to_1);
    CHECK(mfc_committed(n.c[0])->version == v2 && mfc_committed(n.c[2])->version == v2 && mfc_committed(n.c[1])->version == v2 - 1);
    crash(&n, 0);
    elect(&n, 1);
    beat(&n, 2, 1);
    run(&n, all_keep);
    beat(&n, 1, 2);
    run(&n, all_keep);
    CHECK_U(mfc_committed(n.c[2])->version, v2);
    CHECK(mfd_place_get(mfc_committed(n.c[2]), "x") != NULL);
    uint64_t v = first(&n, 1);
    run(&n, all_keep);
    for (uint32_t k = 1; k < 3; k++) CHECK(mfc_committed(n.c[k])->version == v && mfd_place_get(mfc_committed(n.c[k]), "x"));
    net_free(&n);
}

/* A follower that missed a commit while away catches up even past an abandoned proposal. */
/* fleet.rs a_follower_catches_up_past_an_abandoned_proposal */
static void past_abandoned(void) {
    net_t n;
    net_init(&n, 3);
    elect(&n, 0);
    first(&n, 0);
    run(&n, all_keep);
    uint64_t v2 = propose(&n, 0, "a");
    run(&n, not_to_2);
    CHECK(mfc_committed(n.c[0])->version == v2 && mfc_committed(n.c[2])->version == v2 - 1);
    propose(&n, 0, "b");
    run(&n, none_keep);
    n.now += 4000;
    for (uint32_t k = 1; k < 3; k++) out(&n, 0, mfc_link(n.c[0], k, true, n.now));
    out(&n, 0, mfc_tick(n.c[0], n.now));
    CHECK(!mfc_busy(n.c[0]) && mfc_accepted(n.c[0])->version == v2 + 1 && mfc_committed(n.c[0])->version == v2);
    for (int i = 0; i < 3; i++) {
        n.now += 1100;
        beat(&n, 2, 0);
        run(&n, all_keep);
        beat(&n, 0, 2);
        run(&n, all_keep);
    }
    CHECK_U(mfc_committed(n.c[2])->version, v2);
    net_free(&n);
}

/* N = 5: version 2 (x) committed on {0, 1, 2}; two leader changes lose nothing. */
/* fleet.rs a_committed_version_survives_two_leader_changes_n5 */
static void survives_two_changes(void) {
    net_t n;
    net_init(&n, 5);
    elect(&n, 0);
    first(&n, 0);
    run(&n, all_keep);
    propose(&n, 0, "a");
    run(&n, all_keep);
    uint64_t v2 = propose(&n, 0, "x");
    run(&n, n5_filter);
    CHECK(mfd_place_get(mfc_committed(n.c[0]), "x") && mfc_committed(n.c[2])->version == v2);
    crash(&n, 0);
    elect(&n, 1);
    beat(&n, 2, 1);
    run(&n, all_keep);
    beat(&n, 1, 2);
    run(&n, all_keep);
    crash(&n, 1);
    elect(&n, 2);
    uint64_t v = first(&n, 2);
    run(&n, all_keep);
    for (uint32_t k = 2; k < 5; k++) {
        const mfd_doc *d = mfc_committed(n.c[k]);
        CHECK(d->version == v && mfd_place_get(d, "x") && mfd_place_get(d, "a"));
    }
    net_free(&n);
}

static bool fail_persist(void *ctx, char **err) {
    *err = mt_strdup((const char *)ctx);
    return false;
}

static bool count_persist(void *ctx, char **err) {
    (void)err;
    (*(int *)ctx)++;
    return true;
}

static bool has_send(const mfc_outs *o, mfc_msg_kind kind, int to, int granted) {
    for (size_t i = 0; i < o->n; i++)
        if (o->v[i].kind == MFC_OUT_SEND && o->v[i].msg.kind == kind && (to < 0 || o->v[i].to == (uint32_t)to) &&
            (granted < 0 || o->v[i].msg.granted == (bool)granted))
            return true;
    return false;
}

/* Review finding 4: a vote (or an acknowledgement) whose persist failed is never sent. */
/* fleet.rs nothing_leaves_without_its_persist */
static void nothing_leaves_without_persist(void) {
    mfc_core *c = mfc_new((mfc_config){1, 3, 3000, 3}, (mfp_persist){0}, 1);
    mfc_outs o = mfc_link(c, 0, true, 1);
    mfc_outs_free(&o);
    o = mfc_link(c, 2, true, 1);
    mfc_outs_free(&o);
    o = mfc_recv(c, 0, (mfc_msg){.kind = MFC_VOTE_REQ, .pre = false, .term = 5}, 10000);
    CHECK(has_send(&o, MFC_VOTE, 0, 1));
    char *err = NULL;
    CHECK(!mfc_durable(&o, fail_persist, "ENOSPC", &err));
    free(err);
    CHECK(!has_send(&o, MFC_VOTE, -1, -1));
    mfc_outs_free(&o);
    mfc_free(c);
    /* The disk holds what it held before the vote: no vote in term 5. */
    c = mfc_new((mfc_config){1, 3, 3000, 3}, (mfp_persist){0}, 1);
    o = mfc_link(c, 0, true, 1);
    mfc_outs_free(&o);
    o = mfc_link(c, 2, true, 1);
    mfc_outs_free(&o);
    o = mfc_recv(c, 2, (mfc_msg){.kind = MFC_VOTE_REQ, .pre = false, .term = 5}, 10000);
    CHECK(has_send(&o, MFC_VOTE, 2, 1));
    mfc_outs_free(&o);
    /* An accepted document's acknowledgement likewise. */
    mfd_doc d = {.version = 1, .term = 5};
    mfd_place_put(&d, "x", (mt_strs){0});
    o = mfc_recv(c, 2, (mfc_msg){.kind = MFC_STATE, .term = 5, .doc = d}, 10001);
    CHECK(!mfc_durable(&o, fail_persist, "EIO", &err));
    free(err);
    CHECK(!has_send(&o, MFC_STATE_ACK, -1, -1));
    mfc_outs_free(&o);
    /* A disk that works: the persist precedes the sends, which all come out. */
    o = mfc_recv(c, 2, (mfc_msg){.kind = MFC_COMMIT, .term = 5, .ver = {5, 1}}, 10002);
    size_t want = 0, persists = 0;
    for (size_t i = 0; i < o.n; i++) {
        if (o.v[i].kind == MFC_OUT_PERSIST) persists++;
        else want++;
    }
    int wrote = 0;
    CHECK(mfc_durable(&o, count_persist, &wrote, &err));
    CHECK(o.n == want && (size_t)wrote == persists && persists > 0);
    mfc_outs_free(&o);
    mfc_free(c);
}

int main(void) {
    never_regresses();
    past_abandoned();
    survives_two_changes();
    nothing_leaves_without_persist();
    return mesh_check_done();
}
