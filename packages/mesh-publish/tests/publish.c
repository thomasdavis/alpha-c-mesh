/* publish.rs's rings_are_per_slot, full_ring_and_rewind and policies. */
#include "mesh/check.h"
#include "mesh/publish.h"

#include <stdlib.h>
#include <string.h>

static mesh_mailbox mb;

/* Plays a node of slot s: completes ticket t with result r. */
static void complete(uint32_t s, uint32_t t, uint32_t r) {
    size_t c = mesh_prog_ctl(s), e = c + MESH_P_RING + MESH_ENTRY_BYTES * (size_t)(t % MESH_PROG_RING);
    CHECK_U(mesh_mailbox_get(mb, e + MESH_E_SEQ), t + 1);
    size_t k = mesh_mailbox_get(mb, e + MESH_E_SLOT);
    mesh_mailbox_set(mb, c + MESH_P_RESULTS + 4 * k, r);
    mesh_mailbox_publish(mb, c + MESH_P_MARKS + 4 * k, t + 1);
}

static mesh_manifest man(const char *extra) {
    char text[512];
    snprintf(text, sizeof text, "name = \"p\"\nabi = 6\nview_bytes = 0x1000\n%s", extra);
    mesh_manifest m;
    char *err = NULL;
    if (!mesh_manifest_parse(text, strlen(text), &m, &err)) { fprintf(stderr, "%s\n", err); abort(); }
    return m;
}

static size_t collect(mesh_pring *r, uint32_t *ticket, uint32_t *res) {
    mesh_done *d;
    size_t n;
    mesh_pring_collect(r, mb, &d, &n);
    if (n && ticket) *ticket = d[0].out.ticket;
    if (n && res) *res = d[0].result;
    free(d);
    return n;
}

#define MS(x) ((int64_t)(x) * 1000000)

int main(void) {
    mb = mesh_mailbox_test();
    mesh_pring a, b;
    mesh_pring_init(&a, 0);
    mesh_pring_init(&b, 5);
    uint32_t t;
    CHECK(mesh_pring_publish(&a, mb, MESH_OP_TASK, 9, mesh_kind_of(MESH_TK_AUTO), &t) && t == 0);
    CHECK(mesh_pring_publish(&b, mb, 7, 1, mesh_kind_of(MESH_TK_AUTO), &t) && t == 0);
    CHECK(mesh_pring_publish(&b, mb, 7, 2, mesh_kind_of(MESH_TK_AUTO), &t) && t == 1);
    size_t e = mesh_prog_ctl(5) + MESH_P_RING + MESH_ENTRY_BYTES;
    CHECK(mesh_mailbox_get(mb, e + MESH_E_OP) == 7 && mesh_mailbox_get(mb, e + MESH_E_ARG) == 2 && mesh_mailbox_get(mb, e + MESH_E_SEQ) == 2);
    CHECK_U(mesh_mailbox_get64(mb, mesh_prog_ctl(5) + MESH_P_RING_TAIL), 2);
    complete(5, 1, 33);
    uint32_t tk = 99, res = 0;
    CHECK_U(collect(&b, &tk, &res), 1);
    CHECK(tk == 1 && res == 33);
    CHECK(collect(&a, NULL, NULL) == 0 && !mesh_pring_idle(&a));
    complete(0, 0, 34);
    CHECK(collect(&a, NULL, &res) == 1 && res == 34);
    CHECK(mesh_pring_idle(&a));

    /* full_ring_and_rewind */
    mesh_pring r;
    mesh_pring_init(&r, 2);
    int ok = 1;
    for (uint32_t i = 0; i < MESH_PROG_RING; i++) ok &= mesh_pring_publish(&r, mb, 0, i, mesh_kind_of(MESH_TK_AUTO), &t) && t == i;
    CHECK(ok);
    CHECK(!mesh_pring_publish(&r, mb, 0, 0, mesh_kind_of(MESH_TK_AUTO), &t));
    complete(2, 5, 0);
    collect(&r, NULL, NULL);
    CHECK(!mesh_pring_can_publish(&r));
    complete(2, 0, 0);
    collect(&r, NULL, NULL);
    CHECK(mesh_pring_publish(&r, mb, 0, 0, mesh_kind_of(MESH_TK_AUTO), &t) && t == MESH_PROG_RING);
    mesh_done *d;
    size_t n;
    mesh_pring_collect(&r, mb, &d, &n);
    ok = 1;
    for (size_t i = 0; i < n; i++) ok &= d[i].out.ticket != MESH_PROG_RING;
    CHECK(ok);
    free(d);
    mesh_mailbox_set(mb, mesh_prog_ctl(2) + MESH_P_RING_HEAD, r.tail + 7);
    mesh_mailbox_set(mb, mesh_prog_ctl(2) + MESH_P_ONLINE, 3);
    mesh_out *lost;
    mesh_pring_rewind(&r, mb, &lost, &n);
    CHECK_U(n, MESH_PROG_RING - 1);
    free(lost);
    CHECK(mesh_mailbox_get(mb, mesh_prog_ctl(2) + MESH_P_RING_HEAD) == r.tail && mesh_mailbox_get(mb, mesh_prog_ctl(2) + MESH_P_ONLINE) == 0);
    CHECK(mesh_pring_idle(&r));
    mesh_pring_free(&r);

    /* policies */
    int64_t t0 = 1000000000000ll, last = t0;
    mesh_manifest m = man("policy = \"continuous\"\nfanout = 3");
    mesh_pring_init(&r, 0);
    mesh_pring_free(&a);
    CHECK_U(mesh_publish_auto(mb, &m, 0, &r, &last, t0), 3);
    CHECK_U(mesh_publish_auto(mb, &m, 0, &r, &last, t0), 0);
    uint32_t tl = r.tail;
    complete(0, tl - 3, 0);
    complete(0, tl - 2, 0);
    collect(&r, NULL, NULL);
    CHECK_U(mesh_publish_auto(mb, &m, 0, &r, &last, t0), 0);
    complete(0, tl - 1, 0);
    collect(&r, NULL, NULL);
    CHECK_U(mesh_publish_auto(mb, &m, 0, &r, &last, t0), 3);
    mesh_manifest_free(&m);
    mesh_pring_free(&r);
    m = man("policy = \"continuous\"\nfanout = 4\nin_flight = \"free\"");
    mesh_pring_init(&r, 1);
    CHECK_U(mesh_publish_auto(mb, &m, 0, &r, &last, t0), 4);
    complete(1, 1, 0);
    collect(&r, NULL, NULL);
    CHECK_U(mesh_publish_auto(mb, &m, 0, &r, &last, t0), 1);
    CHECK(mesh_pring_publish(&r, mb, 3, 0, mesh_kind_of(MESH_TK_INIT), &t));
    CHECK_U(mesh_publish_auto(mb, &m, 0, &r, &last, t0), 0);
    mesh_manifest_free(&m);
    mesh_pring_free(&r);
    m = man("policy = \"periodic\"\nhz = 10\nfanout = 2\nin_flight = \"free\"\narg = \"view_u32:0x1c\"");
    mesh_mailbox_set(mb, 0x100000 + 0x1c, 40);
    mesh_pring_init(&r, 3);
    last = t0;
    CHECK_U(mesh_publish_auto(mb, &m, 0x100000, &r, &last, t0 + MS(50)), 0);
    CHECK_U(mesh_publish_auto(mb, &m, 0x100000, &r, &last, t0 + MS(100)), 2);
    CHECK(last == t0 + MS(100));
    e = mesh_prog_ctl(3) + MESH_P_RING;
    CHECK(mesh_mailbox_get(mb, e + MESH_E_ARG) == 40 && mesh_mailbox_get(mb, e + MESH_ENTRY_BYTES + MESH_E_ARG) == 41);
    int64_t late = t0 + MS(5000);
    CHECK_U(mesh_publish_auto(mb, &m, 0x100000, &r, &last, late), 2);
    CHECK(last == late);
    CHECK_U(mesh_publish_auto(mb, &m, 0x100000, &r, &last, late), 0);
    mesh_manifest_free(&m);
    mesh_pring_free(&r);
    m = man("policy = \"continuous\"\nfanout = 2");
    mesh_pring_init(&r, 6);
    for (uint32_t i = 0; i < MESH_PROG_RING - 1; i++) mesh_pring_publish(&r, mb, 3, i, mesh_kind_of(MESH_TK_INIT), &t);
    CHECK_U(mesh_pring_room(&r), 1);
    CHECK_U(mesh_publish_auto(mb, &m, 0, &r, &last, late), 0);
    complete(6, 0, 0);
    collect(&r, NULL, NULL);
    CHECK_U(mesh_pring_room(&r), 2);
    CHECK_U(mesh_publish_auto(mb, &m, 0, &r, &last, late), 2);
    mesh_manifest_free(&m);
    mesh_pring_free(&r);
    m = man("");
    mesh_pring_init(&r, 4);
    CHECK_U(mesh_publish_auto(mb, &m, 0, &r, &last, late), 0);
    mesh_manifest_free(&m);
    mesh_pring_free(&r);
    m = man("policy = \"periodic\"\nhz = 10\nfanout = 4");
    mesh_pring_init(&r, 7);
    last = t0;
    CHECK_U(mesh_publish_parts(mb, &m, 0, &r, &last, t0 + MS(100), 3), 2);
    e = mesh_prog_ctl(7) + MESH_P_RING;
    CHECK(mesh_mailbox_get(mb, e + MESH_E_ARG) == 0 && mesh_mailbox_get(mb, e + MESH_ENTRY_BYTES + MESH_E_ARG) == 1);
    CHECK_U(mesh_pring_auto_in_flight(&r), 2);
    CHECK_U(mesh_publish_parts(mb, &m, 0, &r, &last, t0 + MS(200), 3), 0);
    mesh_pring r2;
    mesh_pring_init(&r2, 8);
    int64_t last2 = t0;
    CHECK_U(mesh_publish_parts(mb, &m, 0, &r2, &last2, t0 + MS(100), 9), 2);
    e = mesh_prog_ctl(8) + MESH_P_RING;
    CHECK(mesh_mailbox_get(mb, e + MESH_E_ARG) == 0 && mesh_mailbox_get(mb, e + MESH_ENTRY_BYTES + MESH_E_ARG) == 3);
    mesh_pring r3;
    mesh_pring_init(&r3, 9);
    CHECK_U(mesh_publish_parts(mb, &m, 0, &r3, &last2, t0 + MS(1000), 0), 0);
    CHECK(mesh_pring_publish(&r, mb, MESH_OP_FOLLOW, 2, mesh_kind_of(MESH_TK_FOLLOW), &t));
    CHECK(mesh_pring_auto_in_flight(&r) == 2 && mesh_pring_follow_in_flight(&r) == 1);
    mesh_manifest_free(&m);
    mesh_pring_free(&r);
    mesh_pring_free(&r2);
    mesh_pring_free(&r3);
    mesh_pring_free(&b);
    free(mb.base);
    return mesh_check_done();
}
