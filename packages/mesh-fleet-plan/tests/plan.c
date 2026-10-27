/* fleet.rs reconcile_plans, and seed. */
#include "mesh/check.h"
#include "mesh/fleet_plan.h"
#include "mesh/text.h"

#include <stdlib.h>

static mfd_doc doc(void) {
    mfd_doc d = {.version = 7, .term = 3};
    msn_needs n = msn_default();
    mfd_prog h = mfd_prog_new("hello", 0xabc, true, &n);
    h.reset = 2;
    mfd_prog_put(&d, "hello", h);
    mfd_prog t = mfd_prog_new("train-b", 0x123, true, &n);
    t.lockstep = true;
    t.has_prev = true;
    t.prev_dir = mt_strdup("train");
    t.prev_hash = 0x99;
    mfd_prog_put(&d, "train", t);
    msn_free(&n);
    return d;
}

static bool no_skip(void *ctx, const char *n, uint64_t h) { return (void)ctx, (void)n, (void)h, false; }
static bool skip_hello(void *ctx, const char *n, uint64_t h) { return (void)ctx, (void)h, strcmp(n, "hello") == 0; }

static mfl_action act(mfl_kind k, const char *name, const char *dir, uint64_t hash, bool run, uint32_t from) {
    return (mfl_action){k, (char *)name, (char *)dir, hash, run, from};
}

static void expect(mfl_actions got, const mfl_action *want, size_t n) {
    CHECK_U(got.n, n);
    for (size_t i = 0; i < n && i < got.n; i++) CHECK(mfl_action_eq(&got.v[i], &want[i]));
    mfl_actions_free(&got);
}

static void free_all(mfl_actual *a, size_t n) {
    for (size_t i = 0; i < n; i++) mfl_actual_free(&a[i]);
}

int main(void) {
    mfd_doc d = doc();
    /* Nothing here: load both. */
    mfl_action w1[] = {act(MFL_LOAD, "hello", "hello", 0xabc, true, 0), act(MFL_LOAD, "train", "train-b", 0x123, true, 0)};
    expect(mfl_plan(&d, NULL, 0, no_skip, NULL, NULL, NULL, 0, 0), w1, 2);
    /* Something extra (unload first), hello stopped and behind on resets, train on its previous image. */
    mfl_actual a[] = {mfl_actual_new("pipesrc", "pipesrc", 1, false, 0, true), mfl_actual_new("hello", "hello", 0xabc, false, 0, false),
                      mfl_actual_new("train", "train", 0x99, true, 0x123, true)};
    mfl_action w2[] = {act(MFL_UNLOAD, "pipesrc", NULL, 0, false, 0), act(MFL_ROLLBACK, "train", NULL, 0, false, 0), act(MFL_RUN, "hello", NULL, 0, true, 0),
                       act(MFL_RESET, "hello", NULL, 0, false, 0)};
    expect(mfl_plan(&d, a, 3, no_skip, NULL, NULL, NULL, 0, 0), w2, 4);
    free_all(a, 3);
    /* train elsewhere: an upgrade by bytes; a skipped program is left alone; resets applied. */
    mfl_actual b[] = {mfl_actual_new("hello", "hello", 0xabc, false, 0, true), mfl_actual_new("train", "train", 0x55, true, 0x66, true)};
    const char *rn[] = {"hello"};
    uint32_t rg[] = {2};
    mfl_action w3[] = {act(MFL_UPGRADE, "train", "train-b", 0x123, false, 0)};
    expect(mfl_plan(&d, b, 2, skip_hello, NULL, rn, rg, 1, 0), w3, 1);
    free_all(b, 2);
    /* In sync. */
    mfl_actual c[] = {mfl_actual_new("hello", "hello", 0xabc, false, 0, true), mfl_actual_new("train", "train", 0x123, false, 0, true)};
    expect(mfl_plan(&d, c, 2, no_skip, NULL, rn, rg, 1, 0), NULL, 0);
    /* A failed program that should run is started again. */
    c[0].failed = true;
    mfl_action w4[] = {act(MFL_RUN, "hello", NULL, 0, true, 0)};
    expect(mfl_plan(&d, c, 2, no_skip, NULL, rn, rg, 1, 0), w4, 1);
    c[0].failed = false;
    /* Placed elsewhere: not loaded here, unloaded if it is. */
    mfd_doc e = mfd_doc_clone(&d);
    mfd_prog *h = mfd_prog_get(&e, "hello");
    h->at = ml_some((uint32_t[]){1, 2}, 2);
    mfl_action w5[] = {act(MFL_UNLOAD, "hello", NULL, 0, false, 0)};
    expect(mfl_plan(&e, c, 2, no_skip, NULL, rn, rg, 1, 0), w5, 1);
    expect(mfl_plan(&e, c, 2, no_skip, NULL, rn, rg, 1, 1), NULL, 0);
    free_all(c, 2);
    /* A migration from 2 to 1: the target loads it with the view, the source keeps it as it is. */
    ml_opt_free(&h->at);
    h->at = ml_some((uint32_t[]){1}, 1);
    h->has_from = true;
    h->from = 2;
    mfl_actual only_train[] = {mfl_actual_new("train", "train", 0x123, false, 0, true)};
    mfl_action w6[] = {act(MFL_MIGRATE, "hello", "hello", 0xabc, true, 2)};
    expect(mfl_plan(&e, only_train, 1, no_skip, NULL, rn, rg, 1, 1), w6, 1);
    free_all(only_train, 1);
    mfl_actual stopped[] = {mfl_actual_new("hello", "hello", 0xabc, false, 0, false), mfl_actual_new("train", "train", 0x123, false, 0, true)};
    expect(mfl_plan(&e, stopped, 2, no_skip, NULL, rn, rg, 1, 2), NULL, 0);
    expect(mfl_plan(&e, stopped, 2, no_skip, NULL, rn, rg, 1, 3), w5, 1);
    free_all(stopped, 2);
    mfd_doc_free(&e);
    mfd_doc_free(&d);
    return mesh_check_done();
}
