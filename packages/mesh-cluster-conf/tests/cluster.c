/* cluster.rs's tests: three_systems and errors, ported one for one. */
#include "mesh/check.h"
#include "mesh/cluster_conf.h"
#include "mesh/text.h"

#include <stdlib.h>

#define C3 "# test\nsystems = 3\n0.peer = 10.0.0.1:27800\n0.ssh = local\n1.peer = 10.0.0.1:27801  # same host\n1.listen = 0.0.0.0:27801\n2.peer = \"10.0.0.2:27802\"\n"

static bool fails(const char *text, uint32_t me) {
    mcc_topology t;
    char *err = NULL;
    if (mcc_topology_parse(text, me, &t, &err)) {
        mcc_topology_free(&t);
        return false;
    }
    free(err);
    return true;
}

int main(void) {
    char *err = NULL;
    mcc_topology t;
    /* three_systems */
    CHECK(mcc_topology_parse(C3, 0, &t, &err));
    CHECK(t.n == 3 && t.listen == NULL && t.ndial == 2 && t.dial_k[0] == 1 && t.dial_k[1] == 2);
    CHECK_S(t.dial_addr[0], "10.0.0.1:27801");
    CHECK_S(t.dial_addr[1], "10.0.0.2:27802");
    mcc_topology_free(&t);
    CHECK(mcc_topology_parse(C3, 1, &t, &err));
    CHECK_S(t.listen, "0.0.0.0:27801");
    CHECK(t.ndial == 1 && t.dial_k[0] == 2);
    CHECK_S(t.dial_addr[0], "10.0.0.2:27802");
    mcc_topology_free(&t);
    CHECK(mcc_topology_parse(C3, 2, &t, &err));
    CHECK_S(t.listen, "10.0.0.2:27802");
    CHECK_U(t.ndial, 0);
    mcc_topology_free(&t);
    mcc_topology p = mcc_pair_dialer("h:1");
    mcc_topology want = {.me = 0, .n = 2, .ndial = 1, .tun = mcc_tunables_default()};
    want.dial_k[0] = 1;
    want.dial_addr[0] = mt_strdup("h:1");
    CHECK(mcc_topology_eq(&p, &want));
    mcc_topology_free(&p);
    mcc_topology_free(&want);
    char *c = mt_fmt("%sdead_ms = 1500\nping_ms = 100\n", C3);
    CHECK(mcc_topology_parse(c, 1, &t, &err));
    msw_weights d = msw_default();
    CHECK(t.tun.ping_ms == 100 && t.tun.dead_ms == 1500 && msw_eq(&t.tun.sched, &d));
    mcc_topology_free(&t);
    free(c);
    c = mt_fmt("%ssched_hold_ms = 2000\nsched_improve = 0.5\n", C3);
    CHECK(mcc_topology_parse(c, 1, &t, &err));
    CHECK(t.tun.sched.hold_ms == 2000 && t.tun.sched.improve == 0.5);
    mcc_topology_free(&t);
    free(c);
    c = mt_fmt("%ssched_hold_ms = x\n", C3);
    CHECK(fails(c, 1));
    free(c);
    c = mt_fmt("%sdead_ms = 200\n", C3);
    CHECK(!mcc_topology_parse(c, 1, &t, &err));
    CHECK_S(err, "cluster: dead_ms 200 must be at least 3 x ping_ms 250");
    free(err);
    free(c);
    /* errors */
    CHECK(fails(C3, 3));
    CHECK(fails("systems = 1\n0.peer = a:1", 0));
    CHECK(fails("systems = 9", 0));
    CHECK(fails("systems = 2\n0.peer = a:1", 0));
    CHECK(!mcc_topology_parse("systems = 2\n0.peer = a:1\n1.peer = a:1", 0, &t, &err));
    CHECK_S(err, "cluster: systems 0 and 1 share the peer address a:1");
    free(err);
    CHECK(!mcc_topology_parse("systems = 2\n0.peer = a\n1.peer = b:2", 0, &t, &err));
    CHECK_S(err, "cluster: 0.peer = \"a\": expected host:port");
    free(err);
    ms_map m;
    CHECK(!mcc_keys("systems = 2\nsystems = 3", &m, &err));
    CHECK_S(err, "line 2: systems given twice");
    free(err);
    CHECK(!mcc_keys("junk", &m, &err));
    CHECK_S(err, "line 1: expected key = value, got \"junk\"");
    free(err);
    CHECK(!mcc_keys(" = x", &m, &err));
    CHECK_S(err, "line 1: empty key");
    free(err);
    CHECK(!mcc_topology_parse("systems = 2\n1.peer = b:2", 0, &t, &err));
    CHECK_S(err, "cluster: no `0.peer = host:port`");
    free(err);
    return mesh_check_done();
}
