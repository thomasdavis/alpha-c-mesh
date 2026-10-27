/* jobs.rs's `a_leader_fetches_a_payload_it_lacks` and `leases_follow_cordon_and_the_owner_policy`. */
#include "fsim.h"
#include "mesh/jobs/sched.h"

/* A payload result reaches a leader that lacks its blob: the leader parks it, fetches the blob from its
 * holders, then commits it. */
static void a_leader_fetches_a_payload_it_lacks(void) {
    fsim s;
    fs_init(&s, 1, 79);
    s.payload = true;
    fs_progs(&s, (uint32_t[]){0}, 1, 8);
    fs_step(&s, 50);
    mj_job_id id = fs_submit(&s, 0, "submit p items 200");
    fs_step(&s, 5);
    /* A result whose blob is nowhere here: parked, not committed, a fetch asked for. */
    const char *nowhere = "a payload nowhere here";
    mj_hash h = fs_sha(nowhere, strlen(nowhere));
    uint64_t w[4];
    mj_hash_words(&h, w);
    fpay *p = momap_put(s.bytes, w, NULL);
    p->b = (uint8_t *)mstrdup(nowhere);
    p->n = strlen(nowhere);
    uint64_t lease;
    const mj_lease *x = momap_first(mj_node_leases(s.m[0].node), &lease);
    uint64_t item;
    CHECK(x && mj_ranges_first(&x->out, &item));
    mj_item_res r = {.lease = lease, .job = id, .item = item, .status = MESH_JOBS_RS_OK, .result = (uint32_t)(item * 7 + 1), .has_blob = true,
                     .blob = {h, 9216, 0}};
    mj_outs o = mj_node_result(s.m[0].node, r, s.now);
    mj_outs t = mj_node_tick(s.m[0].node, s.now, s.unix_ms);
    mj_outs_append(&o, &t);
    for (size_t i = 0; i < o.n; i++) CHECK(o.v[i].kind != MJ_OUT_SEND);
    fs_job_out(&s, 0, o);
    CHECK_U(mj_node_staged(s.m[0].node), 1);
    /* The leader's side alone: a result whose blob is not here (holders: a hint). */
    mj_outs out = {0};
    mj_item_res *one = mxalloc(sizeof *one);
    *one = (mj_item_res){.lease = lease, .job = id, .item = item, .status = MESH_JOBS_RS_OK, .result = (uint32_t)(item * 7 + 1), .has_blob = true,
                         .blob = {h, 9216, 0}};
    mj_node_on_results(s.m[0].node, 0, one, 1, s.now, s.unix_ms, &out);
    bool fetched = false;
    for (size_t i = 0; i < out.n; i++) fetched |= out.v[i].kind == MJ_OUT_FETCH_BLOB && mj_hash_eq(&out.v[i].hash, &h) && out.v[i].holders == 0;
    CHECK(fetched);
    mj_outs_free(&out);
    CHECK_U(mj_node_parked(s.m[0].node), 1);
    CHECK(!mj_ranges_contains(&mj_table_get(mj_node_working(s.m[0].node), id)->done, item));
    /* The blob arrives: the parked result is committed (with this leader among its holders). */
    mb_reserved rv;
    CHECK(mb_reserve(s.m[0].blobs, id, h, 22, fs_live, &(fs_live_ctx){&s.m[0].node->rep.committed}, &rv, NULL));
    mb_reserved_free(&rv);
    mb_put_mem(s.m[0].blobs, id, h, nowhere, strlen(nowhere));
    o = mj_node_blob_stored(s.m[0].node, id, h, s.now, s.unix_ms);
    CHECK_U(mj_node_parked(s.m[0].node), 0);
    const mj_res *res = momap_get1(mj_table_get(mj_node_working(s.m[0].node), id)->results, item);
    CHECK(res && res->blob.holders == 1);
    fs_job_out(&s, 0, o);
    mj_job *j = fs_finish(&s, 0, id, 20000);
    fs_exactly_once(j);
    mj_job_free(j);
    fs_free(&s);
}

static mjs_cx cx3(int cord, int busy) {
    mjs_cx c = {0};
    c.members = mxalloc(3 * sizeof *c.members);
    c.nmembers = 3;
    for (uint32_t k = 0; k < 3; k++) {
        msm_member m = msm_member_default();
        m.id = k;
        m.up = true;
        MSM_SET(m.gpu, "cuda");
        MSM_SET(m.arch, "aarch64");
        m.cpus = 8;
        m.ram_mb = 1000;
        m.mem_avail_mb = 800;
        m.gpu_share = mof32_some(0.9f);
        m.load_known = true;
        m.cordoned = (int)k == cord;
        if ((int)k == busy) m.busy = mstrdup("gpu share 0.10");
        c.members[k] = m;
    }
    c.need_names = mxalloc(sizeof *c.need_names);
    c.need_names[0] = mstrdup("p");
    c.needs = mxalloc(sizeof *c.needs);
    c.needs[0] = msn_default();
    c.nneeds = 1;
    c.w = msw_default();
    return c;
}

static void leases_follow_cordon_and_the_owner_policy(void) {
    fsim s;
    fs_init(&s, 3, 71);
    fs_progs(&s, (uint32_t[]){0, 1, 2}, 3, 8);
    fs_step(&s, 4000);
    uint32_t l = (uint32_t)fs_leader(&s);
    uint32_t cord = (l + 1) % 3, busy = (l + 2) % 3;
    for (int k = 0; k < 3; k++) mj_node_set_sched(s.m[k].node, mjs_sched(cx3((int)cord, -1)));
    mj_job_id id = fs_submit(&s, l, "submit p items 6000");
    fs_step(&s, 300);
    /* The owner policy holds `busy` busy while `l` is idle: no new leases there either. */
    for (int k = 0; k < 3; k++) mj_node_set_sched(s.m[k].node, mjs_sched(cx3((int)cord, (int)busy)));
    uint64_t held = 0, lk;
    for (const mj_lease *x = momap_first(mj_node_leases(s.m[l].node), &lk); x; x = momap_next(mj_node_leases(s.m[l].node), &lk, &lk))
        if (x->member == busy) held += mj_ranges_len(&x->out);
    uint64_t done_then = s.m[busy].ran;
    mj_job *j = fs_finish(&s, l, id, 60000);
    fs_exactly_once(j);
    mj_job_free(j);
    CHECK_U(s.m[cord].ran, 0); /* the cordoned member ran no items */
    if (!CHECK(s.m[busy].ran <= done_then + held))
        fprintf(stderr, "busy member: %llu -> %llu (held %llu)\n", (unsigned long long)done_then, (unsigned long long)s.m[busy].ran, (unsigned long long)held);
    CHECK(s.m[l].ran > 3000);
    fs_free(&s);
}

int main(void) {
    a_leader_fetches_a_payload_it_lacks();
    leases_follow_cordon_and_the_owner_policy();
    return mesh_check_done();
}
