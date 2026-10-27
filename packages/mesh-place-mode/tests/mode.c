/* place.rs: modes_and_assignments, roles_input_authority, auto_weights_the_slow_machine_down. */
#include "mesh/check.h"
#include "mesh/place_mode.h"
#include "mesh/text.h"

#include <stdlib.h>

static bool target(const char *w, uint32_t n, mpm_target *t) {
    mt_strs ws = mt_words(w);
    char *err = NULL;
    bool ok = mpm_parse(ws.v, ws.n, n, 4, t, &err);
    free(err);
    mt_strs_free(&ws);
    return ok;
}

static mpm_mode split(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return mpm_split(mpm_assign_new((uint32_t[]){a, b, c, d}, 4)); }

static void check_split(const char *w, uint32_t n, mpm_mode want) {
    mpm_target t;
    CHECK(target(w, n, &t) && !t.is_auto && mpm_mode_eq(&t.mode, &want));
}

static void check_list(const uint32_t *sys, const uint32_t *ns, size_t n, const char *want) {
    mpm_assign a = mpm_auto(sys, ns, n, 4);
    char *l = mpm_list(&a);
    CHECK_S(l, want);
    free(l);
}

int main(void) {
    mpm_target t;
    CHECK(target("pin 2", 3, &t) && t.mode.kind == MPM_PIN && t.mode.pin == 2);
    CHECK(!target("pin 3", 3, &t) && !target("pin", 3, &t) && !target("both", 3, &t));
    check_split("split", 2, split(0, 0, 1, 1));
    check_split("split", 3, split(0, 0, 1, 2));
    check_split("split", 4, split(0, 1, 2, 3));
    check_split("split", 8, split(0, 1, 2, 3));
    check_split("split 2,1", 3, split(2, 2, 1, 1));
    check_split("split 0,2,1", 4, split(0, 0, 2, 1));
    check_split("split 1:3,2:1", 3, split(1, 1, 1, 2));
    check_split("split 0,0,1,2", 3, split(0, 0, 1, 2));
    check_split("split 2,0,2,2", 3, split(2, 0, 2, 2));
    CHECK(target("split auto", 3, &t) && t.is_auto && t.auto_mask == 7);
    const char *bad[] = {"split 0,0", "split 0,1,2,3,0", "split 1:2,2:1", "split 1:2,1:2", "split 0:0,1:4", "split 0,3", "split x"};
    for (int i = 0; i < 7; i++) CHECK(!target(bad[i], 3, &t));
    char *err = NULL;
    mt_strs w = mt_words("split 1:2,1:2");
    CHECK(!mpm_parse(w.v, w.n, 3, 4, &t, &err));
    CHECK_S(err, "split 1:2,1:2: a system twice");
    free(err);
    mt_strs_free(&w);
    w = mt_words("pin 7");
    CHECK(!mpm_parse(w.v, w.n, 3, 4, &t, &err));
    CHECK_S(err, "\"7\": not a system of this cluster (0..2)");
    free(err);
    mt_strs_free(&w);
    w = mt_words("frob");
    CHECK(!mpm_parse(w.v, w.n, 3, 4, &t, &err));
    CHECK_S(err, "usage: place <name> [local | pin 0..2 | split [auto | a,b,.. | a:parts,b:parts,..]], not \"frob\"");
    free(err);
    mt_strs_free(&w);
    mpm_assign a = mpm_assign_new((uint32_t[]){0, 0, 1, 2}, 4);
    char *l = mpm_list(&a);
    CHECK(mpm_owner(&a) == 0 && mpm_mask_of(&a, 0) == 3 && mpm_mask_of(&a, 1) == 4 && mpm_mask_of(&a, 2) == 8 && mpm_mask_of(&a, 3) == 0 && mpm_members(&a) == 7);
    CHECK_S(l, "0,0,1,2");
    free(l);
    /* Wire codes: every mode round-trips; bad ones are refused. */
    mpm_mode modes[] = {mpm_local(), mpm_pin(0), mpm_pin(7), split(0, 0, 1, 2), split(3, 1, 1, 1)};
    for (int i = 0; i < 5; i++) {
        uint32_t c, s;
        uint8_t ab[MESH_PLACE_PARTS_MAX];
        mpm_code(&modes[i], &c, &s);
        mpm_assign_bytes(&modes[i], ab);
        mpm_mode back;
        CHECK(mpm_from_code(c, s, ab, MESH_PLACE_PARTS_MAX, &back, &err) && mpm_mode_eq(&back, &modes[i]));
    }
    mpm_mode s4 = split(0, 1, 2, 3);
    uint8_t gap[MESH_PLACE_PARTS_MAX];
    mpm_assign_bytes(&s4, gap);
    gap[1] = MESH_PART_NONE;
    mpm_mode m;
    CHECK(!mpm_from_code(MESH_PLACE_SPLIT, 0, gap, MESH_PLACE_PARTS_MAX, &m, &err));
    CHECK_S(err, "split: a gap in the assignment");
    free(err);
    mpm_mode s2 = mpm_split(mpm_assign_new((uint32_t[]){0, 1}, 2));
    uint8_t b2[MESH_PLACE_PARTS_MAX];
    mpm_assign_bytes(&s2, b2);
    CHECK(!mpm_from_code(MESH_PLACE_SPLIT, 1, b2, MESH_PLACE_PARTS_MAX, &m, &err));
    free(err);
    CHECK(!mpm_from_code(MESH_PLACE_PIN, 8, NULL, 0, &m, &err));
    free(err);
    CHECK(!mpm_from_code(7, 0, NULL, 0, &m, &err));
    CHECK_S(err, "bad placement 7/0");
    free(err);
    mpm_mode s09 = mpm_split(mpm_assign_new((uint32_t[]){0, 9}, 2)), s01 = mpm_split(mpm_assign_new((uint32_t[]){0, 1}, 2)), p3 = mpm_pin(3);
    CHECK(!mpm_fits(&s09, 3, 2, &err));
    free(err);
    CHECK(!mpm_fits(&s01, 3, 4, &err));
    free(err);
    CHECK(!mpm_fits(&p3, 3, 4, &err));
    free(err);
    mpm_mode lab = split(0, 0, 1, 2);
    l = mpm_label(&lab);
    CHECK_S(l, "split 0,0,1,2");
    free(l);
    mpm_mode p0 = mpm_pin(0), loc = mpm_local();
    CHECK(!mpm_allowed(&s01, MESH_PLACE_CAP_PIN, &err));
    free(err);
    CHECK(mpm_allowed(&p0, MESH_PLACE_CAP_PIN, &err));
    CHECK(!mpm_allowed(&loc, MESH_PLACE_CAP_NONE, &err));
    free(err);

    /* roles_input_authority */
    mpm_mode s = split(1, 1, 0, 2);
    CHECK(mpm_role_of(&s, 0) == MPM_ROLE_FOLLOWER && mpm_role_of(&s, 1) == MPM_ROLE_OWNER && mpm_role_of(&s, 2) == MPM_ROLE_FOLLOWER &&
          mpm_role_of(&s, 3) == MPM_ROLE_MIRROR);
    mpm_mode pin2 = mpm_pin(2);
    CHECK(mpm_role_of(&pin2, 2) == MPM_ROLE_RUNNER && mpm_role_of(&pin2, 0) == MPM_ROLE_MIRROR && mpm_role_of(&loc, 1) == MPM_ROLE_LOCAL);
    CHECK(!mpm_runs(MPM_ROLE_MIRROR) && mpm_runs(MPM_ROLE_FOLLOWER));
    CHECK(mpm_input_to(&loc, 3) == 3 && mpm_input_to(&pin2, 0) == 2 && mpm_input_to(&s, 3) == 1);
    CHECK(mpm_parts_of(&s, 1, 4) == 3 && mpm_parts_of(&s, 3, 4) == 0 && mpm_parts_of(&pin2, 2, 4) == 15 && mpm_parts_of(&pin2, 0, 4) == 0);
    CHECK(mpm_authority(&p0, &pin2) == 0 && mpm_authority(&loc, &s) == 1 && mpm_authority(&s, &loc) == 1 && mpm_authority(&loc, &pin2) == 2);

    /* auto_weights_the_slow_machine_down */
    check_list((uint32_t[]){0, 1, 2, 3}, (uint32_t[]){100, 100, 100, 100}, 4, "0,1,2,3");
    check_list((uint32_t[]){0, 1}, (uint32_t[]){100, 100}, 2, "0,0,1,1");
    check_list((uint32_t[]){0, 1, 2}, (uint32_t[]){100, 100, 150}, 3, "0,0,1,2");
    check_list((uint32_t[]){0, 1, 2}, (uint32_t[]){150, 100, 100}, 3, "1,1,0,2");
    check_list((uint32_t[]){0, 1, 2}, (uint32_t[]){100, 100, 300}, 3, "0,0,1,1");
    check_list((uint32_t[]){0, 1, 2}, (uint32_t[]){300, 100, 100}, 3, "1,1,2,2");
    check_list((uint32_t[]){0, 1}, (uint32_t[]){100, 1000}, 2, "0,0,0,0");
    check_list((uint32_t[]){0, 1, 2}, (uint32_t[]){0, 0, 0}, 3, "0,0,1,2");
    return mesh_check_done();
}
