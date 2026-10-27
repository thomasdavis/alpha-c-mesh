/* jobs.rs's fleet tests, part 1: a single meshd, three members exactly once, priorities, duplicates,
 * a failing item, a job of two programs, the fixed pair's restarted coordinator, a submit that waits
 * for its program, deadline and cancel, a coordinator restarted with its clock behind. */
#include "fsim.h"
#include "mesh/jobs/lid.h"

static const uint32_t ALL3[] = {0, 1, 2};

static void a_single_meshd_runs_a_job(void) {
    fsim s;
    fs_init(&s, 1, 3);
    fs_progs(&s, (uint32_t[]){0}, 1, 8);
    fs_step(&s, 50);
    mj_job_id id = fs_submit(&s, 0, "submit p items 5000");
    mj_job *j = fs_finish(&s, 0, id, 20000);
    fs_exactly_once(j);
    CHECK_S(mj_job_state(j), "done");
    CHECK_U(j->dupes, 0);
    mj_job_free(j);
    /* A list of items and the `none` mode: only which are done. */
    id = fs_submit(&s, 0, "submit p items 3,10-20,40 results none");
    j = fs_finish(&s, 0, id, 5000);
    char *w = mj_ranges_word_new(&j->done);
    CHECK_S(w, "3-4,10-20,40-41");
    free(w);
    CHECK_U(momap_len(j->results), 0);
    mj_job_free(j);
    /* The table survives a restart from the log. */
    fs_restart(&s, 0);
    CHECK_U(momap_len(s.m[0].node->rep.committed.jobs), 2);
    fs_free(&s);
}

static void three_members_share_a_job_exactly_once(void) {
    fsim s;
    fs_init(&s, 3, 11);
    fs_progs(&s, ALL3, 3, 8);
    fs_step(&s, 4000);
    int l = fs_leader(&s);
    CHECK(l >= 0);
    mj_job_id id = fs_submit(&s, (uint32_t)l, "submit p items 20000");
    mj_job *j = fs_finish(&s, (uint32_t)l, id, 60000);
    fs_exactly_once(j);
    /* Every member ran some, and every member's committed copy agrees. */
    for (int k = 0; k < 3; k++)
        if (!CHECK(s.m[k].ran > 1000)) fprintf(stderr, "member %d ran %llu\n", k, (unsigned long long)s.m[k].ran);
    fs_step(&s, 500);
    for (int k = 0; k < 3; k++) CHECK(mj_job_eq(mj_table_get(&s.m[k].node->rep.committed, id), j));
    mj_job_free(j);
    fs_free(&s);
}

static void priorities_go_first(void) {
    fsim s;
    fs_init(&s, 1, 9);
    fs_progs(&s, (uint32_t[]){0}, 1, 4);
    s.m[0].speed = 0.2;
    fs_step(&s, 50);
    mj_job_id low = fs_submit(&s, 0, "submit p items 2000 priority 0");
    fs_step(&s, 200);
    mj_job_id high = fs_submit(&s, 0, "submit p items 200 priority 9");
    mj_job *jh = fs_finish(&s, 0, high, 60000);
    const mj_job *jl = mj_table_get(&s.m[0].node->rep.committed, low);
    /* The high-priority job finished while most of the low one was still to do. */
    CHECK(mj_ranges_len(&jl->done) < 1000);
    fs_exactly_once(jh);
    mj_job_free(jh);
    mj_job *j2 = fs_finish(&s, 0, low, 60000);
    fs_exactly_once(j2);
    mj_job_free(j2);
    fs_free(&s);
}

static void duplicates_are_counted_and_dropped(void) {
    fsim s;
    fs_init(&s, 1, 13);
    fs_progs(&s, (uint32_t[]){0}, 1, 8);
    fs_step(&s, 50);
    mj_job_id id = fs_submit(&s, 0, "submit p items 100");
    mj_job *j = fs_finish(&s, 0, id, 10000);
    /* Late results of item 5: another lease's (a duplicate execution), the same lease's (a re-send). */
    uint64_t lease = ((const mj_res *)momap_get1(j->results, 5))->lease;
    mj_job_free(j);
    fs_job_out(&s, 0, mj_node_result(s.m[0].node, (mj_item_res){.lease = lease + 1000, .job = id, .item = 5, .result = 12345}, s.now));
    fs_job_out(&s, 0, mj_node_result(s.m[0].node, (mj_item_res){.lease = lease, .job = id, .item = 5, .result = 12345}, s.now));
    fs_step(&s, 500);
    /* The job had finished: late results are dropped, the result stays. */
    CHECK_U(((const mj_res *)momap_get1(mj_table_get(&s.m[0].node->rep.committed, id)->results, 5))->result, 36);
    /* During a job: a duplicate execution is counted. */
    id = fs_submit(&s, 0, "submit p items 1000");
    fs_step(&s, 30);
    const mj_job *jw = mj_table_get(mj_node_working(s.m[0].node), id);
    uint64_t x;
    const mj_res *first = momap_first(jw->results, &x);
    CHECK(first != NULL);
    lease = first->lease;
    fs_job_out(&s, 0, mj_node_result(s.m[0].node, (mj_item_res){.lease = lease + 77, .job = id, .item = x, .result = 1}, s.now));
    fs_job_out(&s, 0, mj_node_result(s.m[0].node, (mj_item_res){.lease = lease, .job = id, .item = x, .result = 2}, s.now));
    j = fs_finish(&s, 0, id, 20000);
    fs_exactly_once(j);
    CHECK_U(j->dupes, 1); /* one duplicate execution (the re-send of the same lease is not one) */
    mj_job_free(j);
    fs_free(&s);
}

static void a_failing_item_is_retried_then_failed(void) {
    fsim s;
    fs_init(&s, 3, 17);
    fs_progs(&s, ALL3, 3, 8);
    for (int k = 0; k < 3; k++) {
        s.m[k].has_poison = true;
        s.m[k].poison = 77;
    }
    fs_step(&s, 4000);
    uint32_t l = (uint32_t)fs_leader(&s);
    mj_job_id id = fs_submit(&s, l, "submit p items 500 attempts 3");
    mj_job *j = fs_finish(&s, l, id, 60000);
    CHECK_S(mj_job_state(j), "done-with-failures");
    CHECK_U(momap_len(j->failed), 1);
    const mj_failed *f = momap_get1(j->failed, 77);
    CHECK(f && f->tries == 3 && strstr(f->why, "poison"));
    CHECK_U(momap_len(j->results), 499);
    CHECK(momap_get1(j->results, 77) == NULL);
    mj_job_free(j);
    fs_free(&s);
}

static void a_job_of_two_programs(void) {
    fsim s;
    fs_init(&s, 3, 83);
    fs_progs(&s, (uint32_t[]){0}, 1, 8);
    fs_set_progs(&s, 1, (const char *[]){"q"}, (uint32_t[]){3}, (uint32_t[]){4}, 1);
    fs_set_progs(&s, 2, (const char *[]){"p", "q"}, (uint32_t[]){0, 3}, (uint32_t[]){8, 4}, 2);
    fs_step(&s, 4000);
    uint32_t l = (uint32_t)fs_leader(&s);
    mj_job_id id = fs_submit(&s, l, "submit p,q items 20000");
    mj_job *j = fs_finish(&s, l, id, 120000);
    fs_exactly_once(j);
    CHECK_S(j->spec.program, "p,q");
    for (uint32_t k = 0; k < 3; k++)
        if (!CHECK(mj_count_get(j->by_member, k) > 1000)) fprintf(stderr, "member %u ran %llu\n", k, (unsigned long long)mj_count_get(j->by_member, k));
    mj_job_free(j);
    mj_req r;
    char *err = NULL;
    CHECK(!mj_req_parse((const char *[]){"submit", "p,", "items", "3"}, 4, &r, &err));
    free(err);
    fs_free(&s);
}

static void the_fixed_pair_restarted_coordinator(void) {
    fsim s;
    fs_init(&s, 2, 51);
    fs_progs(&s, (uint32_t[]){0, 1}, 2, 8);
    fs_step(&s, 500);
    mj_job_id id = fs_submit(&s, 0, "submit p items 8000");
    fs_step(&s, 1000);
    fs_crash(&s, 0);
    fs_step(&s, 100);
    fs_restart(&s, 0);
    fs_progs(&s, (uint32_t[]){0}, 1, 8);
    mj_job *j = fs_finish(&s, 0, id, 60000);
    fs_exactly_once(j);
    fs_step(&s, 500);
    CHECK(mj_job_eq(mj_table_get(&s.m[1].node->rep.committed, id), j));
    mj_job_free(j);
    /* Job ids stay unique across the coordinator's restart (term 1 both times). */
    mj_job_id id2 = fs_submit(&s, 0, "submit p items 10");
    CHECK(id2 > id);
    fs_free(&s);
}

static void a_submit_waits_for_its_program(void) {
    fsim s;
    fs_init(&s, 1, 81);
    fs_step(&s, 200);
    /* Submitted before the program runs: held, then accepted once the member reports it. */
    uint64_t t = fs_ask(&s, 0, "submit p items 100");
    fs_step(&s, 500);
    fs_progs(&s, (uint32_t[]){0}, 1, 8);
    bool ok;
    char *a = fs_answer(&s, t, 2000, &ok);
    CHECK(ok);
    char w[64];
    mj_job_id id = 0;
    CHECK(sscanf(a, "job %63s", w) == 1 && mj_parse_job(w, &id));
    free(a);
    mj_job *j = fs_finish(&s, 0, id, 10000);
    fs_exactly_once(j);
    mj_job_free(j);
    /* A program nobody runs: refused after SUBMIT_WAIT_MS. */
    t = fs_ask(&s, 0, "submit q items 10");
    char *e = fs_answer(&s, t, MESH_JOBS_SUBMIT_WAIT_MS + 1000, &ok);
    CHECK(!ok && strstr(e, "no member runs q"));
    free(e);
    fs_free(&s);
}

static void deadline_and_cancel(void) {
    fsim s;
    fs_init(&s, 1, 61);
    fs_progs(&s, (uint32_t[]){0}, 1, 8);
    s.m[0].speed = 0.1;
    fs_step(&s, 50);
    mj_job_id id = fs_submit(&s, 0, "submit p items 100000 deadline 1");
    mj_job *j = fs_finish(&s, 0, id, 5000);
    CHECK_S(mj_job_state(j), "deadline");
    CHECK(mj_ranges_len(&j->done) < 1000);
    mj_job_free(j);
    id = fs_submit(&s, 0, "submit p items 0- results none");
    fs_step(&s, 500);
    char line[64], w[MJ_JOB_WORD_MAX];
    mj_job_word(id, w);
    snprintf(line, sizeof line, "cancel %s", w);
    bool ok;
    char *a = fs_answer(&s, fs_ask(&s, 0, line), 2000, &ok);
    CHECK(ok);
    free(a);
    j = fs_finish(&s, 0, id, 2000);
    CHECK_S(mj_job_state(j), "cancelled");
    mj_job_free(j);
    /* Forget it. */
    snprintf(line, sizeof line, "forget %s", w);
    a = fs_answer(&s, fs_ask(&s, 0, line), 2000, &ok);
    CHECK(ok);
    free(a);
    CHECK(mj_table_get(&s.m[0].node->rep.committed, id) == NULL);
    fs_free(&s);
}

/* Review finding 10: the leadership id and the lease ids come from the persisted log, not the wall
 * clock: the pair's coordinator restarted with its clock an hour behind leads above its old
 * leadership, and its leases never reuse an id. */
static void a_restarted_coordinator_with_its_clock_behind(void) {
    fsim s;
    fs_init(&s, 2, 51);
    fs_progs(&s, (uint32_t[]){0, 1}, 2, 8);
    fs_step(&s, 500);
    mj_job_id id = fs_submit(&s, 0, "submit p items 8000");
    fs_step(&s, 1000);
    uint64_t lid = mj_node_lid(s.m[0].node);
    uint64_t before_max = 0;
    size_t before_n[2] = {s.m[0].ngot, s.m[1].ngot};
    for (int k = 0; k < 2; k++)
        for (size_t i = 0; i < s.m[k].ngot; i++)
            if (s.m[k].got[i].lease > before_max) before_max = s.m[k].got[i].lease;
    /* The leases each member was sent so far (its got list survives in the harness only until a restart). */
    uint64_t *seen = NULL;
    size_t nseen = 0;
    for (int k = 0; k < 2; k++)
        for (size_t i = 0; i < before_n[k]; i++) {
            seen = mxrealloc(seen, (nseen + 1) * sizeof *seen);
            seen[nseen++] = s.m[k].got[i].lease;
        }
    fs_crash(&s, 0);
    fs_step(&s, 100);
    s.unix_ms -= 3600000;
    fs_restart(&s, 0);
    fs_progs(&s, (uint32_t[]){0}, 1, 8);
    fs_step(&s, 200);
    CHECK(mj_node_lid(s.m[0].node) > lid && mj_lid_term(mj_node_lid(s.m[0].node)) == mj_lid_term(lid));
    mj_job *j = fs_finish(&s, 0, id, 60000);
    fs_exactly_once(j);
    mj_job_free(j);
    size_t after = 0;
    bool reused = false;
    for (int k = 0; k < 2; k++)
        for (size_t i = 0; i < s.m[k].ngot; i++) {
            bool old = false;
            for (size_t q = 0; q < nseen; q++) old |= seen[q] == s.m[k].got[i].lease;
            if (old) continue;
            after++;
            if (s.m[k].got[i].lease <= before_max) reused = true;
        }
    CHECK(after > 0 && !reused);
    free(seen);
    mj_job_id id2 = fs_submit(&s, 0, "submit p items 10");
    CHECK(id2 > id);
    fs_free(&s);
}

int main(void) {
    a_single_meshd_runs_a_job();
    three_members_share_a_job_exactly_once();
    priorities_go_first();
    duplicates_are_counted_and_dropped();
    a_failing_item_is_retried_then_failed();
    a_job_of_two_programs();
    the_fixed_pair_restarted_coordinator();
    a_submit_waits_for_its_program();
    deadline_and_cancel();
    a_restarted_coordinator_with_its_clock_behind();
    return mesh_check_done();
}
