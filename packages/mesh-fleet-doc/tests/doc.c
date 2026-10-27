/* fleet.rs document_round_trips (the document part), group rank/order, can_run, decode errors. */
#include "mesh/check.h"
#include "mesh/fleet_doc.h"

#include <stdlib.h>

static mfd_doc doc(void) {
    mfd_doc d = {.version = 7, .term = 3};
    msn_needs n = msn_default();
    mfd_prog h = mfd_prog_new("hello", 0xabc, true, &n);
    h.reset = 2;
    mt_push(&h.backends, "cuda");
    mt_push(&h.backends, "wasm");
    mfd_prog_put(&d, "hello", h);
    mfd_prog t = mfd_prog_new("train-b", 0x123, true, &n);
    t.lockstep = true;
    mt_push(&t.backends, "cuda");
    t.has_prev = true;
    t.prev_dir = mt_strdup("train");
    t.prev_hash = 0x99;
    mfd_prog_put(&d, "train", t);
    msn_free(&n);
    mfd_group g = {.name = mt_strdup("train"), .program = mt_strdup("train"), .epoch = 4, .cycle = 2, .op = {MFD_OP_TASK, 3, 1000593162},
                   .members = ml_of((uint32_t[]){0, 2, 3}, 3), .state = MFD_RUNNING, .on_loss = MFD_LOSS_REFORM, .auto_ms = 5000};
    mfd_group_push(&d, g);
    mt_strs w = {0};
    mt_push(&w, "split");
    mt_push(&w, "auto");
    mfd_place_put(&d, "doom", w);
    return d;
}

static void round_trip(const mfd_doc *d) {
    char *e = mfd_encode(d);
    mfd_doc back;
    char *err = NULL;
    CHECK(mfd_decode(e, &back, &err));
    CHECK(mfd_doc_eq(&back, d));
    mfd_doc_free(&back);
    free(e);
}

static void bad(const char *text, const char *want) {
    mfd_doc d;
    char *err = NULL;
    CHECK(!mfd_decode(text, &d, &err));
    if (want) CHECK_S(err, want);
    free(err);
}

int main(void) {
    mfd_doc d = doc();
    round_trip(&d);
    char *e = mfd_encode(&d);
    CHECK_S(e, "doc version 7 term 3\n"
               "program hello dir hello hash 0000000000000abc run 1 reset 2 lockstep 0 backends cuda,wasm prev - 0 at * from - pin * needs "
               "b=cuda;ar=-;mm=0;pf=-;av=-;r=all;sp=spread;co=-;fd=-;g=-;gs=0;gm=-;pr=0;cpu=0;mem=0;mg=state;ls=0;vb=0;w=1;sh=0 why \n"
               "program train dir train-b hash 0000000000000123 run 1 reset 0 lockstep 1 backends cuda prev train 0000000000000099 at * from - pin * needs "
               "b=cuda;ar=-;mm=0;pf=-;av=-;r=all;sp=spread;co=-;fd=-;g=-;gs=0;gm=-;pr=0;cpu=0;mem=0;mg=state;ls=0;vb=0;w=1;sh=0 why \n"
               "group train program train epoch 4 cycle 2 op task:3:1000593162 members 0,2,3 state running on_loss reform auto_ms 5000\n"
               "place doom split,auto\n"
               "sched paused 0 autoreform 0 cordon -\n");
    free(e);
    mfd_doc empty = {0};
    round_trip(&empty);
    /* The scheduler's fields: a placement, a migration, a pin, the needs, the why; the sched flags. */
    mfd_doc x = mfd_doc_clone(&d);
    mfd_prog *h = mfd_prog_get(&x, "hello");
    ml_opt_free(&h->at);
    h->at = ml_some((uint32_t[]){1, 3}, 2);
    h->has_from = h->has_to = true;
    h->from = 2;
    h->to = 3;
    h->since = 41;
    h->pin = ml_some((uint32_t[]){3}, 1);
    h->needs.replicas = (msn_replicas){false, 2};
    msn_terms_push(&h->needs.prefer, msn_term_new(MSN_HOST, "dgx-00", 0));
    mt_set(&h->why, "replicas 2: system 1 0.812, system 3 0.700; next system 0 0.6");
    mfd_prog *t = mfd_prog_get(&x, "train");
    t->at = ml_some(NULL, 0);
    x.sched_paused = true;
    x.autoreform = true;
    x.cordon = 9;
    round_trip(&x);
    /* A migration's source alone (no target). */
    h->has_to = false;
    h->since = 0;
    round_trip(&x);
    mfd_doc_free(&x);
    bad("program x", "fleet document line 1: \"program x\"");
    bad("", "fleet document: no `doc version .. term ..` line");
    bad("doc version 1 term 1\ngroup g program p epoch 1 cycle 0 op bogus members - state running on_loss pause auto_ms 0", "bad op \"bogus\"");
    bad("doc version 1 term 1\ngroup g program p epoch 1 cycle 0 op none members 9 state running on_loss pause auto_ms 0", "member 9 >= 8");
    bad("doc version 1 term 1\ngroup g program p epoch 1 cycle 0 op none members - state sleeping on_loss pause auto_ms 0", NULL);
    bad("doc version x term 1", "fleet document line 1: \"doc version x term 1\"");
    /* Group rank and order. */
    uint32_t r;
    CHECK(mfd_group_rank(&d.groups[0], 2, &r) && r == 1 && !mfd_group_rank(&d.groups[0], 1, &r));
    uint8_t o[MESH_MAX_SYSTEMS];
    mfd_group_order(&d.groups[0], o);
    CHECK(o[0] == 0 && o[1] == 2 && o[2] == 3 && o[3] == 0xff && o[7] == 0xff);
    CHECK_U(mfd_group_mask(&d.groups[0]), 0xd);
    /* here: placed there, or the source of a migration. */
    mfd_prog *hp = mfd_prog_get(&d, "hello");
    CHECK(mfd_prog_here(hp, 5));
    hp->at = ml_some((uint32_t[]){1}, 1);
    CHECK(mfd_prog_here(hp, 1) && !mfd_prog_here(hp, 2));
    hp->has_from = true;
    hp->from = 2;
    CHECK(mfd_prog_here(hp, 2));
    /* can_run: the member's GPU, or wasm / native anywhere. */
    mt_strs b = {0};
    mt_push(&b, "metal");
    CHECK(mfd_can_run(&b, 1) && !mfd_can_run(&b, 0) && !mfd_can_run(&b, 99));
    mt_push(&b, "native");
    CHECK(mfd_can_run(&b, 0));
    mt_strs_free(&b);
    /* Maps: replace, remove. */
    CHECK(mfd_prog_remove(&d, "hello") && !mfd_prog_remove(&d, "hello") && d.nprogs == 1);
    CHECK(mfd_place_remove(&d, "doom") && mfd_place_get(&d, "doom") == NULL);
    CHECK(mfd_group_of(&d, "train") && mfd_group_named(&d, "train") && !mfd_group_of(&d, "x"));
    mfd_group_remove_at(&d, 0);
    CHECK_U(d.ngroups, 0);
    CHECK(mfd_id_cmp(3, 9, 4, 1) < 0 && mfd_id_cmp(4, 2, 4, 1) > 0 && mfd_id_cmp(1, 1, 1, 1) == 0);
    mfd_doc_free(&d);
    return mesh_check_done();
}
