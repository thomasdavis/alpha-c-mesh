/* JobSpec's text form both ways (jobs.rs's `ops_frames_and_the_merge` starts with it), and what decode
 * refuses, with the Rust's messages. */
#include "mesh/jobs/spec.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    mj_setup su = {6, 0};
    mj_spec s = {.id = (uint64_t)1 << 32 | 1, .program = mstrdup("p"), .op = 0xfe, .items = mj_ranges_of(0, 10), .priority = 0,
                 .attempts = 3, .mode = MJ_MODE_COLLECT, .setup = mj_setup_clone(&su, 1), .nsetup = 1, .submitted_ms = 5, .members = 5};
    msb b = {0};
    mj_spec_encode(&s, &b);
    CHECK_S(msb_str(&b), "id 4294967297 program p op 254 items 0-10 priority 0 attempts 3 deadline 0 mode collect setup 6:0 submitted 5 members 0x5");
    mj_spec d = {0};
    char *err = NULL;
    CHECK(mj_spec_decode(msb_str(&b), &d, &err) && mj_spec_eq(&d, &s));
    mj_spec_free(&d);
    /* No setup, members 0, an endless set, a negative priority; and whitespace of every kind. */
    const char *t = "id 7\tprogram a,b op 3 items 5-,1 priority -2 attempts 1 deadline 9 mode none setup - submitted 0 members 0x0\n";
    CHECK(mj_spec_decode(t, &d, &err));
    CHECK(mj_spec_endless(&d) && d.priority == -2 && d.nsetup == 0 && d.members == 0 && d.mode == MJ_MODE_NONE);
    CHECK(mj_spec_runs(&d, "a") && mj_spec_runs(&d, "b") && !mj_spec_runs(&d, "a,b") && !mj_spec_runs(&d, ""));
    const char *fp;
    CHECK_U(mj_spec_first_program(&d, &fp), 1);
    CHECK(fp[0] == 'a');
    msb_clear(&b);
    mj_spec_encode(&d, &b);
    CHECK_S(msb_str(&b), "id 7 program a,b op 3 items 1-2,5- priority -2 attempts 1 deadline 9 mode none setup - submitted 0 members 0x0");
    mj_spec_free(&d);
    /* Refusals: a missing word, a bad number, a bad mode, setup without a colon. */
    CHECK(!mj_spec_decode("id 7", &d, &err));
    CHECK_S(err, "job spec \"id 7\"");
    free(err);
    CHECK(!mj_spec_decode("id x program p op 1 items 1 priority 0 attempts 1 deadline 0 mode none setup - submitted 0 members 0x0", &d, &err));
    free(err);
    CHECK(!mj_spec_decode("id 1 program p op 1 items 1 priority 0 attempts 1 deadline 0 mode all setup - submitted 0 members 0x0", &d, &err));
    free(err);
    CHECK(!mj_spec_decode("id 1 program p op 1 items 1 priority 0 attempts 1 deadline 0 mode none setup 6 submitted 0 members 0x0", &d, &err));
    free(err);
    /* A bad item set says so in Ranges' words. */
    CHECK(!mj_spec_decode("id 1 program p op 1 items 5-5 priority 0 attempts 1 deadline 0 mode none setup - submitted 0 members 0x0", &d, &err));
    CHECK(strncmp(err, "items \"5-5\"", 11) == 0);
    free(err);
    /* members: repeated 0x prefixes are trimmed, as trim_start_matches does. */
    CHECK(mj_spec_decode("id 1 program p op 1 items 1 priority 0 attempts 1 deadline 0 mode none setup - submitted 0 members 0x0xff", &d, &err));
    CHECK_U(d.members, 255);
    mj_spec_free(&d);
    mj_mode m;
    CHECK(mj_mode_parse("nonzero", &m) && m == MJ_MODE_NONZERO && !mj_mode_parse("Collect", &m));
    mj_spec_free(&s);
    msb_free(&b);
    return mesh_check_done();
}
