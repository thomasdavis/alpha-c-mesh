/* crates/mesh-top/tests/snap.rs, ported: snapshot parsing against mesh-abi's constants (framing,
 * sections, fields, the host block) and the trace reply. */
#include "mesh/check.h"
#include "mesh/top_snap.h"

#include <stdlib.h>
#include <string.h>

static void parts(uint8_t *head, mt_section secs[2], uint8_t *host) {
    memset(head, 0, MESH_H_BYTES);
    mt_put_u32(head, MESH_H_EPOCH, 9);
    mt_section a = mt_section_new(3, 2, 0x41); /* an odd view length: padded to 0x44 */
    mt_put_str(mt_section_entry(&a), MESH_PT_NAME, MESH_NAME_BYTES, "life");
    mt_put_u32(mt_section_entry(&a), MESH_PT_STATE, MESH_ST_RUNNING);
    mt_put_u64(mt_section_entry(&a), MESH_PT_PTX_HASH, 0x123456789abcdef0u);
    mt_put_u32(mt_section_ctl(&a), MESH_P_ONLINE, 2);
    mt_put_u32(mt_section_ctl(&a), MESH_P_COUNTERS + 4, 77);
    mt_put_str(mt_section_ext(&a), MESH_PX_COUNTER_NAMES + MESH_NAME_BYTES, MESH_NAME_BYTES, "births");
    mt_put_u32(mt_section_ext(&a), MESH_PX_FIELD_COUNT, 3);
    const char *names[3] = {"gen", "rate", "tail"};
    uint32_t types[3] = {MESH_FT_U32, MESH_FT_F32, MESH_FT_U64};
    uint32_t offs[3] = {0, 4, 0x3c};
    for (size_t i = 0; i < 3; i++) {
        size_t at = MESH_PX_FIELDS + MESH_FIELD_BYTES * i;
        mt_put_str(mt_section_ext(&a), at + MESH_FD_NAME, MESH_FD_NAME_MAX + 1, names[i]);
        mt_put_u32(mt_section_ext(&a), at + MESH_FD_TYPE, types[i]);
        mt_put_u32(mt_section_ext(&a), at + MESH_FD_OFF, offs[i]);
    }
    mt_put_u32(mt_section_node_rec(&a, 1), MESH_N_SM, 33);
    mt_put_u64(mt_section_node_rec(&a, 1), MESH_N_BUSY_NS, 0x100000002u);
    mt_put_u32(mt_section_view(&a), 0, 42);
    mt_put_f32(mt_section_view(&a), 4, 1.5f);
    mt_section_view(&a)[0x40] = 0xab;
    mt_section b = mt_section_new(0, 0, 0);
    mt_put_str(mt_section_entry(&b), MESH_PT_NAME, MESH_NAME_BYTES, "idle");
    mt_put_u32(mt_section_entry(&b), MESH_PT_STATE, MESH_ST_LOADED);
    memset(host, 0, MESH_SNAP_HOST);
    mt_put_str(host, MESH_S_HOSTNAME, 32, "dgx-00");
    mt_put_f32(host, MESH_S_RATES + 4 * 3, 1234.5f);
    mt_put_u64(host, MESH_S_LOCK_STEP, (uint64_t)1 << 40);
    mt_put_str(host, MESH_S_LOCK_NAME, MESH_NAME_BYTES, "train");
    host[MESH_S_LOCK_VIEW + 31] = 7;
    secs[0] = a;
    secs[1] = b;
}

static bool parses(const uint8_t *b, size_t len, char **err) {
    mt_snapshot s;
    *err = NULL;
    bool ok = mt_snapshot_parse(b, len, &s, err);
    if (ok) mt_snapshot_free(&s);
    return ok;
}

int main(void) {
    static uint8_t head[MESH_H_BYTES], host[MESH_SNAP_HOST];
    mt_section secs[2];
    parts(head, secs, host);
    mt_snapshot s = mt_snapshot_assemble(head, secs, 2, host);
    CHECK_U(s.nsections, 2);
    CHECK_U(mt_section_slot(&s.sections[0]), 0);
    CHECK_U(mt_section_slot(&s.sections[1]), 3);
    CHECK_U(s.len, MESH_SN_HEAD_BYTES + MESH_H_BYTES + mesh_section_bytes(0, 0) + mesh_section_bytes(2, 0x41) + MESH_SNAP_HOST);
    CHECK_U(mt_snapshot_head(&s, MESH_H_EPOCH), 9);
    const mt_section *p = mt_snapshot_by_name(&s, "life");
    CHECK(p != NULL);
    CHECK_U(mt_section_slot(p), 3);
    CHECK_S(mt_section_state_name(p), "running");
    CHECK(mt_section_running(p));
    CHECK_U(mt_section_nodes(p), 2);
    CHECK_U(mt_section_ptx_hash(p), 0x123456789abcdef0u);
    char cn[64];
    CHECK_U(mt_section_counter(p, 1), 77);
    mt_section_counter_name(p, 1, cn);
    CHECK_S(cn, "births");
    mt_section_counter_name(p, 0, cn);
    CHECK_S(cn, "");
    mt_field f[MESH_MAX_FIELDS];
    CHECK_U(mt_section_fields(p, f), 3);
    char v[64];
    CHECK_S(f[0].name, "gen");
    mt_section_field_value(p, &f[0], v, sizeof v);
    CHECK_S(v, "42");
    mt_section_field_value(p, &f[1], v, sizeof v);
    CHECK_S(v, "1.500000");
    mt_section_field_value(p, &f[2], v, sizeof v); /* a u64 at 0x3c needs 0x44 bytes; only 0x41 were copied */
    CHECK_S(v, "-");
    CHECK_U(mt_section_view_len(p), 0x41);
    CHECK_U(mt_section_view(p)[0x40], 0xab);
    CHECK_U(mt_section_view_u32(p, 0x40), 0);
    mt_node n = mt_section_node(p, 1);
    CHECK_U(n.sm, 33);
    CHECK_U(n.busy_ns, 0x100000002u);
    CHECK_U(mt_section_node(p, 5).sm, 0);
    CHECK_U(mt_snapshot_nodes_online(&s), 2);
    CHECK(!mt_section_running(mt_snapshot_by_slot(&s, 0)));
    char hn[97];
    mt_snapshot_hostname(&s, hn);
    CHECK_S(hn, "dgx-00");
    CHECK(mt_snapshot_rate(&s, 3) == 1234.5f);
    char ln[64];
    uint64_t step;
    uint8_t view[32];
    mt_snapshot_lock_pair(&s, ln, &step, view);
    CHECK_S(ln, "train");
    CHECK_U(step, (uint64_t)1 << 40);
    CHECK_U(view[31], 7);

    /* Bad framing is refused. */
    char *err;
    uint8_t *good = malloc(s.len);
    memcpy(good, s.bytes, s.len);
    size_t len = s.len;
    CHECK(parses(good, len, &err));
    CHECK(!parses(good, len - 4, &err));
    free(err);
    uint8_t zeros[64] = {0};
    CHECK(!parses(zeros, 64, &err));
    free(err);
    size_t first = MESH_SN_HEAD_BYTES + MESH_H_BYTES;
    struct {
        size_t at;
        uint32_t v;
    } bad[] = {{MESH_SN_VERSION, 1}, {MESH_SN_COUNT, 1}, {MESH_SN_COUNT, 3}, {first + MESH_SC_BYTES, 0x999},
               {first + MESH_SC_NODES, 40}, {first + MESH_SC_SLOT, 3}, {MESH_SN_HOST_AT, 0x200}};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        uint8_t *b = malloc(len);
        memcpy(b, good, len);
        mt_put_u32(b, bad[i].at, bad[i].v);
        CHECK(!parses(b, len, &err));
        if (i == 0) CHECK(err && strstr(err, "version"));
        free(err);
        free(b);
    }
    free(good);
    mt_snapshot empty = mt_snapshot_assemble(head, NULL, 0, host);
    CHECK_U(empty.nsections, 0);
    mt_snapshot_free(&empty);
    mt_snapshot_free(&s);
    mt_section_free(&secs[0]);
    mt_section_free(&secs[1]);

    /* The trace reply. */
    size_t per = 8 + (size_t)MESH_TRACE_PER_PROG * MESH_TRACE_BYTES;
    uint8_t *t = calloc(2, per);
    uint8_t *ring = t + 8;
    mt_put_u32(t, 0, 2);
    mt_put_u32(t, 4, 130);
    for (uint32_t k = 0; k < 130; k++) {
        size_t at = MESH_TRACE_BYTES * (k % MESH_TRACE_PER_PROG);
        mt_put_u32(ring, at, 2 | (k % 3) << 8 | MESH_OP_TASK << 16);
        mt_put_u32(ring, at + 4, k);
        mt_put_u32(ring, at + 8, (UINT32_MAX - 500) + 10 * k);
        mt_put_u32(ring, at + 12, (UINT32_MAX - 500) + 10 * k + 1000 + k);
    }
    uint8_t *t5 = t + per;
    mt_put_u32(t5, 0, 5);
    mt_put_u32(t5, 4, 3);
    mt_put_u32(t5 + 8, 0, 5 | 1 << 8 | 4 << 16);
    mt_trace_prog *v2;
    size_t nv;
    CHECK(mt_parse_trace(t, 2 * per, &v2, &nv, &err));
    CHECK_U(nv, 2);
    CHECK(v2[0].slot == 2 && v2[0].n == 130 && v2[0].nrecs == 128);
    CHECK(v2[0].recs[0].ticket == 2 && v2[0].recs[127].ticket == 129);
    CHECK(v2[0].recs[0].node == 2 && v2[0].recs[0].op == MESH_OP_TASK && mt_rec_ns(&v2[0].recs[0]) == 1002);
    CHECK_U(v2[1].nrecs, 3);
    CHECK(v2[1].recs[0].slot == 5 && v2[1].recs[0].node == 1 && v2[1].recs[0].op == 4);
    double p50;
    CHECK(mt_quantile_us(v2[0].recs, v2[0].nrecs, 0.5, &p50) && p50 >= 1.06 && p50 < 1.07);
    CHECK(!mt_quantile_us(NULL, 0, 0.5, &p50));
    mt_trace_free(v2, nv);
    CHECK(!mt_parse_trace(t, 2 * per - 1, &v2, &nv, &err));
    free(err);
    mt_put_u32(t, 0, 99);
    CHECK(!mt_parse_trace(t, 2 * per, &v2, &nv, &err));
    free(err);
    free(t);
    return mesh_check_done();
}
