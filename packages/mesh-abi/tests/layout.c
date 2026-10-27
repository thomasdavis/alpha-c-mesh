/* The Rust const asserts that span packages and call functions, the FRAMES guard, and the Rust's
 * layout_tests::regions_have_no_aliased_fields, over the whole contract. */
#include "mesh/abi.h"
#include "mesh/check.h"

int main(void) {
    CHECK(mesh_disjoint(MESH_HEADER_FIELDS, MESH_HEADER_FIELDS_LEN)); /* disjoint(&HEADER_FIELDS) */
    CHECK(mesh_disjoint(MESH_EXT_FIELDS, MESH_EXT_FIELDS_LEN)); /* disjoint(&EXT_FIELDS) */
    CHECK(mesh_disjoint(MESH_HOST_FIELDS, MESH_HOST_FIELDS_LEN)); /* disjoint(&HOST_FIELDS) */
    CHECK(mesh_disjoint(MESH_MEMBER_FIELDS, MESH_MEMBER_FIELDS_LEN)); /* disjoint(&MEMBER_FIELDS) */
    CHECK(mesh_disjoint(MESH_LINK_FIELDS, MESH_LINK_FIELDS_LEN)); /* disjoint(&LINK_FIELDS) */
    CHECK(mesh_disjoint(MESH_NIC_FIELDS, MESH_NIC_FIELDS_LEN)); /* disjoint(&NIC_FIELDS) */
    CHECK(mesh_disjoint(MESH_CTL_FIELDS, MESH_CTL_FIELDS_LEN)); /* disjoint(&CTL_FIELDS) */
    CHECK(mesh_fields_end(MESH_CTL_FIELDS, MESH_CTL_FIELDS_LEN) <= MESH_LINE); /* fields_end(&CTL_FIELDS) <= LINE */
    CHECK(mesh_disjoint(MESH_NODE_FIELDS, MESH_NODE_FIELDS_LEN)); /* disjoint(&NODE_FIELDS) */
    CHECK(mesh_fields_end(MESH_NODE_FIELDS, MESH_NODE_FIELDS_LEN) <= MESH_NODE_STRIDE); /* fields_end(&NODE_FIELDS) <= NODE_STRIDE */
    CHECK(mesh_disjoint(MESH_NODE_LOAD_FIELDS, MESH_NODE_LOAD_FIELDS_LEN)); /* disjoint(&NODE_LOAD_FIELDS) */
    CHECK(mesh_fields_end(MESH_NODE_LOAD_FIELDS, MESH_NODE_LOAD_FIELDS_LEN) <= MESH_NL_STRIDE); /* fields_end(&NODE_LOAD_FIELDS) <= NL_STRIDE */
    CHECK(mesh_disjoint(MESH_MLOAD_FIELDS, MESH_MLOAD_FIELDS_LEN)); /* disjoint(&MLOAD_FIELDS) */
    CHECK(mesh_fields_end(MESH_MLOAD_FIELDS, MESH_MLOAD_FIELDS_LEN) <= MESH_MLOAD_BYTES); /* fields_end(&MLOAD_FIELDS) <= MLOAD_BYTES */
    CHECK(mesh_disjoint(MESH_PLOAD_FIELDS, MESH_PLOAD_FIELDS_LEN)); /* disjoint(&PLOAD_FIELDS) */
    CHECK(mesh_fields_end(MESH_PLOAD_FIELDS, MESH_PLOAD_FIELDS_LEN) <= MESH_PLOAD_BYTES); /* fields_end(&PLOAD_FIELDS) <= PLOAD_BYTES */
    CHECK(mesh_prog_ext(MESH_PROG_SLOTS ) + 0x8000 <= MESH_NODE_LOAD); /* prog_ext(PROG_SLOTS as u32) + 0x8000 <= NODE_LOAD */
    CHECK(mesh_node_load(MESH_PROG_SLOTS , 0) <= MESH_LOAD_PAGE); /* node_load(PROG_SLOTS as u32, 0) <= LOAD_PAGE */
    CHECK(mesh_load_page(MESH_S_LOAD_END) <= MESH_PIPE_TABLE); /* load_page(S_LOAD_END) <= PIPE_TABLE */
    CHECK(mesh_pipe_entry(MESH_PROG_SLOTS ) <= MESH_VIEW_HEAP); /* pipe_entry(PROG_SLOTS as u32) <= VIEW_HEAP */
    CHECK(mesh_disjoint(MESH_PIPE_FIELDS, MESH_PIPE_FIELDS_LEN)); /* disjoint(&PIPE_FIELDS) */
    CHECK(mesh_fields_end(MESH_PIPE_FIELDS, MESH_PIPE_FIELDS_LEN) <= MESH_PP_BYTES); /* fields_end(&PIPE_FIELDS) <= PP_BYTES */
    CHECK(mesh_nic(MESH_MAX_NICS) <= MESH_S_LOAD); /* nic(MAX_NICS) <= S_LOAD */
    CHECK(mesh_mload(MESH_MAX_SYSTEMS) <= 0x1400); /* mload(MAX_SYSTEMS) <= 0x1400 */
    CHECK(mesh_pload(MESH_PROG_SLOTS ) == MESH_S_LOAD_END); /* pload(PROG_SLOTS as u32) == S_LOAD_END */
    CHECK(mesh_mload(MESH_MAX_SYSTEMS) <= MESH_S_FLEET); /* mload(MAX_SYSTEMS) <= S_FLEET */
    CHECK(mesh_fmember(MESH_MAX_SYSTEMS) <= MESH_S_GROUP); /* fmember(MAX_SYSTEMS) <= S_GROUP */
    CHECK(mesh_placed(MESH_PROG_SLOTS ) <= MESH_S_SCHED_END); /* placed(PROG_SLOTS as u32) <= S_SCHED_END */
    CHECK(mesh_frame_kind(MESH_F_JOB_STATE) == MESH_KIND_LOAD); /* frame_kind(F_JOB_STATE) == KIND_LOAD */
    CHECK(mesh_frame_kind(MESH_F_RESULT) == MESH_KIND_LOAD); /* frame_kind(F_RESULT) == KIND_LOAD */
    CHECK(mesh_frame_kind(MESH_F_VIEW) == MESH_KIND_LOAD); /* frame_kind(F_VIEW) == KIND_LOAD */
    CHECK(mesh_frame_kind(MESH_F_BLOB) == MESH_KIND_LOAD); /* frame_kind(F_BLOB) == KIND_LOAD */
    CHECK(mesh_disjoint(MESH_FMEMBER_FIELDS, MESH_FMEMBER_FIELDS_LEN)); /* disjoint(&FMEMBER_FIELDS) */
    CHECK(mesh_fields_end(MESH_FMEMBER_FIELDS, MESH_FMEMBER_FIELDS_LEN) <= MESH_FMEMBER_BYTES); /* fields_end(&FMEMBER_FIELDS) <= FMEMBER_BYTES */
    CHECK(mesh_fields_end(MESH_HEADER_FIELDS, MESH_HEADER_FIELDS_LEN) <= MESH_H_BYTES); /* fields_end(&HEADER_FIELDS) <= H_BYTES */
    CHECK(mesh_fields_end(MESH_EXT_FIELDS, MESH_EXT_FIELDS_LEN) <= MESH_PX_BYTES); /* fields_end(&EXT_FIELDS) <= PX_BYTES */
    CHECK(mesh_fields_end(MESH_HOST_FIELDS, MESH_HOST_FIELDS_LEN) <= MESH_SNAP_HOST); /* fields_end(&HOST_FIELDS) <= SNAP_HOST */
    CHECK(mesh_fields_end(MESH_MEMBER_FIELDS, MESH_MEMBER_FIELDS_LEN) <= MESH_MEMBER_BYTES); /* fields_end(&MEMBER_FIELDS) <= MEMBER_BYTES */
    CHECK(mesh_fields_end(MESH_LINK_FIELDS, MESH_LINK_FIELDS_LEN) <= MESH_LINK_BYTES); /* fields_end(&LINK_FIELDS) <= LINK_BYTES */
    CHECK(mesh_fields_end(MESH_NIC_FIELDS, MESH_NIC_FIELDS_LEN) <= MESH_NIC_BYTES); /* fields_end(&NIC_FIELDS) <= NIC_BYTES */
    CHECK(mesh_member(MESH_MAX_SYSTEMS) <= MESH_S_NET_AT_MS); /* member(MAX_SYSTEMS) <= S_NET_AT_MS */
    CHECK(mesh_link(MESH_MAX_SYSTEMS) <= MESH_S_NICS); /* link(MAX_SYSTEMS) <= S_NICS */
    CHECK(mesh_nic(MESH_MAX_NICS) <= MESH_SNAP_HOST); /* nic(MAX_NICS) <= SNAP_HOST */
    CHECK(mesh_frame_kind(MESH_F_ROUND) == MESH_KIND_ROUND); /* frame_kind(F_ROUND) == KIND_ROUND */
    CHECK(mesh_frame_kind(MESH_F_FRAME) == MESH_KIND_PLACE); /* frame_kind(F_FRAME) == KIND_PLACE */
    CHECK(mesh_frame_kind(MESH_F_STATE) == MESH_KIND_LOAD); /* frame_kind(F_STATE) == KIND_LOAD */
    CHECK(mesh_frame_kind(MESH_F_BEAT) == MESH_KIND_CTL); /* frame_kind(F_BEAT) == KIND_CTL */
    CHECK(mesh_frame_kind(MESH_F_JOIN) == MESH_KIND_CTL); /* frame_kind(F_JOIN) == KIND_CTL */
    CHECK(mesh_prog_entry(MESH_PROG_SLOTS ) <= MESH_PROG_CTL); /* prog_entry(PROG_SLOTS as u32) <= PROG_CTL */
    CHECK(mesh_prog_ctl(MESH_PROG_SLOTS ) <= MESH_PROG_NODES); /* prog_ctl(PROG_SLOTS as u32) <= PROG_NODES */
    CHECK(mesh_trace_rec(MESH_PROG_SLOTS  - 1, MESH_TRACE_PER_PROG - 1) + MESH_TRACE_BYTES <= MESH_EX_BASE); /* trace_rec(PROG_SLOTS as u32 - 1, TRACE_PER_PROG - 1) + TRACE_BYTES <= EX_BASE */
    CHECK(mesh_ex_counter(MESH_MAX_SYSTEMS) <= mesh_ex_slots(0)); /* ex_counter(MAX_SYSTEMS) <= ex_slots(0) */
    CHECK(mesh_ex_slots(MESH_MAX_SYSTEMS - 1) + MESH_EX_REGION_BYTES <= MESH_EX_BASE + MESH_EX_BYTES); /* ex_slots(MAX_SYSTEMS - 1) + EX_REGION_BYTES <= EX_BASE + EX_BYTES */
    CHECK(mesh_prog_ext(MESH_PROG_SLOTS ) <= MESH_VIEW_HEAP); /* prog_ext(PROG_SLOTS as u32) <= VIEW_HEAP */
    CHECK(mesh_member(MESH_MAX_SYSTEMS) <= MESH_SNAP_HOST); /* member(MAX_SYSTEMS) <= SNAP_HOST */
    /* Every peer frame type, in id order: unique and dense (1..=32). */
    for (size_t i = 0; i < MESH_FRAMES_LEN; i++) CHECK_U(MESH_FRAMES[i], i + 1);
    /* regions_have_no_aliased_fields */
    struct { const char *name; const mesh_field *f; size_t n; } regions[] = {
        {"header", MESH_HEADER_FIELDS, MESH_HEADER_FIELDS_LEN}, {"extension", MESH_EXT_FIELDS, MESH_EXT_FIELDS_LEN},
        {"host", MESH_HOST_FIELDS, MESH_HOST_FIELDS_LEN}, {"member", MESH_MEMBER_FIELDS, MESH_MEMBER_FIELDS_LEN},
        {"link", MESH_LINK_FIELDS, MESH_LINK_FIELDS_LEN}, {"nic", MESH_NIC_FIELDS, MESH_NIC_FIELDS_LEN},
        {"node", MESH_NODE_FIELDS, MESH_NODE_FIELDS_LEN}, {"node load", MESH_NODE_LOAD_FIELDS, MESH_NODE_LOAD_FIELDS_LEN},
        {"member load", MESH_MLOAD_FIELDS, MESH_MLOAD_FIELDS_LEN}, {"program load", MESH_PLOAD_FIELDS, MESH_PLOAD_FIELDS_LEN},
        {"control", MESH_CTL_FIELDS, MESH_CTL_FIELDS_LEN}, {"pipe", MESH_PIPE_FIELDS, MESH_PIPE_FIELDS_LEN},
        {"fleet member", MESH_FMEMBER_FIELDS, MESH_FMEMBER_FIELDS_LEN},
    };
    for (size_t i = 0; i < sizeof regions / sizeof regions[0]; i++)
        if (!CHECK(mesh_disjoint(regions[i].f, regions[i].n))) fprintf(stderr, "%s: two fields overlap\n", regions[i].name);
    return mesh_check_done();
}
