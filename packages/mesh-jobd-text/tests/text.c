/* The jobs answers over a single meshd's node that runs a job (leases fed back as results). */
#include "mesh/jobd/text.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

static mj_view single = {1, true, 0, 1, 1};

/* Runs every lease the outputs hold (results item*7+1, item 3 failing), returns the leases seen. */
static void act(mj_node *n, mj_outs o, uint64_t now) {
    for (size_t i = 0; i < o.n; i++) {
        if (o.v[i].kind != MJ_OUT_RUN) continue;
        mj_lease_msg *l = &o.v[i].run;
        for (size_t r = 0; r < l->items.n; r++)
            for (uint64_t x = l->items.v[r][0]; x < l->items.v[r][1]; x++) {
                mj_item_res res = {.lease = l->lease, .job = l->job, .item = x, .status = x == 3 ? MESH_JOBS_RS_FAIL : MESH_JOBS_RS_OK,
                                   .result = (uint32_t)(x * 7 + 1)};
                if (x == 3) {
                    res.out = (uint8_t *)mstrdup("boom");
                    res.out_len = 4;
                }
                mj_outs ro = mj_node_result(n, res, now);
                mj_outs_free(&ro);
            }
    }
    mj_outs_free(&o);
}

int main(void) {
    mj_replica r;
    mj_replica_init(&r);
    mj_node *n = mj_node_new((mj_cfg){0, 1, 1, 3000}, r);
    uint64_t now = 1, unix_ms = 1000000;
    act(n, mj_node_set_view(n, single, now, unix_ms), now);
    mj_prog *p = mxalloc(sizeof *p);
    *p = (mj_prog){mstrdup("p"), 0, 8};
    mj_node_set_progs(n, p, 1);
    now++;
    unix_ms++;
    act(n, mj_node_tick(n, now, unix_ms), now);
    const char *w[] = {"submit", "p", "items", "10", "attempts", "1"};
    mj_req req;
    CHECK(mj_req_parse(w, 6, &req, NULL));
    act(n, mj_node_request(n, req, 1, now, unix_ms), now);
    mjd_text_cx cx = {n, single, false, unix_ms};
    char *t = mjd_jobs_text(&cx);
    CHECK(strncmp(t, "jobs system 0 leader 0 role leader lid 10000000000 ", 51) == 0);
    CHECK(strstr(t, "job 1.1 program p state running priority 0 items 10 done 0 failed 0 leased ") != NULL);
    free(t);
    for (int i = 0; i < 400; i++) {
        now += 5;
        unix_ms += 5;
        act(n, mj_node_tick(n, now, unix_ms), now);
    }
    cx.unix_ms = unix_ms;
    t = mjd_jobs_text(&cx);
    CHECK(strstr(t, "job 1.1 program p state done-with-failures priority 0 items 10 done 9 failed 1 leased 0 queued - dupes 0 mode collect elapsed_ms ") != NULL);
    CHECK(strstr(t, "rate system 0 program p items_per_s ") != NULL);
    free(t);
    char *s = NULL;
    CHECK(mjd_status_text(&cx, (uint64_t)1 << 32 | 1, &s));
    CHECK(strstr(s, "\nspec id 4294967297 program p op 254 items 0-10 priority 0 attempts 1 deadline 0 mode collect setup - submitted ") != NULL);
    CHECK(strstr(s, "\nmember 0 done 9\n") != NULL);
    CHECK(strstr(s, "\nfailed 3 tries 1 system 0: boom\n") != NULL);
    CHECK(strstr(s, "\nretried 1 item(s)\n") != NULL);
    free(s);
    CHECK(mjd_results_text(&cx, (uint64_t)1 << 32 | 1, 2, 3, &s));
    CHECK_S(s, "results 1.1 total 9 from 2 count 3 done 9 failed 1 dupes 0 state done-with-failures\n2 15 lease 10000000001 member 0\n4 29 lease 10000000001 member 0\n5 36 lease 10000000002 member 0\n");
    free(s);
    CHECK(!mjd_status_text(&cx, 7, &s));
    CHECK_S(s, "no job 0.7 (committed here)");
    free(s);
    mj_node_free(n);
    return mesh_check_done();
}
