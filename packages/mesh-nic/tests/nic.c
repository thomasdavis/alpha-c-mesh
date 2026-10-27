/* net.rs's tests: proc_net_dev, ext_wraps_and_resets, total_is_monotonic_and_skips_virtual,
 * loopback_address_is_on_lo, this_machine_has_interfaces, nic_record_layout. */
#include "mesh/check.h"
#include "mesh/nic.h"

#include <stdlib.h>
#include <string.h>

static const char *DEV = "Inter-|   Receive                                                |  Transmit\n"
                         " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
                         "    lo: 1000      10    0    0    0     0          0         0     1000      10    0    0    0     0       0          0\n"
                         "enP7s7: 18446744073709551000 200 1 2 0 0 0 0 3000 300 3 4 0 0 0 0\n"
                         "docker0: 50 1 0 0 0 0 0 0 60 1 0 0 0 0 0 0\n";

static mesh_nic_raw raw(const char *name, uint64_t b) {
    mesh_nic_raw r;
    memset(&r, 0, sizeof r);
    strcpy(r.name, name);
    r.v[0] = b;
    r.v[1] = 2 * b;
    for (int i = 0; i < 8; i++) r.bits[i] = 64;
    return r;
}

static void sample(mesh_nics *s, const mesh_nic_raw *r, size_t n, mesh_nic_now **now, size_t *nn) {
    if (*now) free(*now);
    mesh_nics_sample(s, r, n, now, nn);
}

int main(void) {
    mesh_nic_raw *r;
    size_t n;
    char *err = NULL;
    CHECK_I(mesh_nic_parse_proc_net_dev(DEV, &r, &n, &err), 0);
    CHECK_U(n, 3);
    CHECK_S(r[1].name, "enP7s7");
    const uint64_t want[8] = {18446744073709551000ull, 3000, 200, 300, 1, 3, 2, 4};
    CHECK(memcmp(r[1].v, want, sizeof want) == 0);
    free(r);
    CHECK_I(mesh_nic_parse_proc_net_dev("a\nb\n", &r, &n, &err), -1);
    free(err);
    CHECK_I(mesh_nic_parse_proc_net_dev("a\nb\neth0: 1 2 3\n", &r, &n, &err), -1);
    CHECK_S(err, "/proc/net/dev line 3: 3 fields, want 16");
    free(err);
    CHECK_I(mesh_nic_parse_proc_net_dev("a\nb\neth0 1 2\n", &r, &n, &err), -1);
    CHECK_S(err, "/proc/net/dev line 3: no ':'");
    free(err);

    mesh_nic_ext e = mesh_nic_ext_new(0xfffffff0u);
    CHECK_U(mesh_nic_ext_update(&e, 0x10, 32), 0x20);
    CHECK_U(e.value, 0x100000010ull);
    CHECK_U(mesh_nic_ext_update(&e, 0x30, 32), 0x20);
    e = mesh_nic_ext_new(1000);
    CHECK_U(mesh_nic_ext_update(&e, 1500, 64), 500);
    CHECK_U(mesh_nic_ext_update(&e, 200, 64), 200);
    CHECK_U(e.value, 1700);
    e = mesh_nic_ext_new(UINT64_MAX - 5);
    CHECK_U(mesh_nic_ext_update(&e, UINT64_MAX, 64), 5);

    mesh_nics s = {0};
    mesh_nic_now *now = NULL;
    size_t nn;
    mesh_nic_raw a[3] = {raw("lo", 100), raw("en0", 1000), raw("veth1", 50)};
    sample(&s, a, 3, &now, &nn);
    CHECK_U(s.total[0], 1000);
    CHECK_U(s.total[1], 2000);
    CHECK_U(s.counted, 1);
    mesh_nic_raw b[3] = {raw("lo", 900), raw("en0", 1100), raw("en1", 5000)};
    sample(&s, b, 3, &now, &nn);
    CHECK_U(s.total[0], 1100);
    CHECK_U(mesh_nic_lookup(now, nn, "en1")[0], 5000);
    mesh_nic_raw c[2] = {raw("en0", 1200), raw("en1", 5100)};
    sample(&s, c, 2, &now, &nn);
    CHECK_U(s.total[0], 1300);
    mesh_nic_raw d[2] = {raw("en0", 50), raw("en1", 5100)};
    sample(&s, d, 2, &now, &nn);
    CHECK_U(s.total[0], 1350);
    mesh_nic_raw f[1] = {raw("en1", 5200)};
    sample(&s, f, 1, &now, &nn);
    CHECK_U(s.total[0], 1450);
    free(now);
    mesh_nics_free(&s);

    char name[MESH_NIC_NAME_MAX];
    CHECK_I(mesh_nic_iface_of("127.0.0.1", name, &err), 0);
    CHECK(strncmp(name, "lo", 2) == 0);
    CHECK_I(mesh_nic_iface_of("203.0.113.77", name, &err), -1);
    CHECK_S(err, "no interface holds 203.0.113.77");
    free(err);

    CHECK_I(mesh_nic_read(&r, &n, &err), 0);
    int lo = 0;
    for (size_t i = 0; i < n; i++) lo |= strncmp(r[i].name, "lo", 2) == 0;
    CHECK(lo);
    free(r);

    uint8_t rec[MESH_NIC_BYTES];
    const uint64_t v[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    mesh_nic_record(rec, "enP2p1s0f1np1-long", v, 6, 1);
    CHECK(memcmp(rec, "enP2p1s0f1np1-l\0", 16) == 0);
    CHECK_U(rec[MESH_NI_TX_DROP], 8);
    CHECK_U(rec[MESH_NI_LINKS], 6);
    return mesh_check_done();
}
