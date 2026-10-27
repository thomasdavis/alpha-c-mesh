/* mesh-top-mock (crates/mesh-top examples/mock_meshd.rs): a mock meshd control port for trying
 * mesh-top without a GPU: `mesh-top-mock 127.0.0.1:47700 dgx-00 0`. The data is synthetic (see
 * @ajax/mesh-top-mock); it is a viewer fixture only. */
#include "mesh/top_mock.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *addr = argc > 1 ? argv[1] : "127.0.0.1:47700";
    const char *host = argc > 2 ? argv[2] : "mock";
    uint64_t sys;
    uint32_t system = argc > 3 && mt_parse_u64(argv[3], &sys) && sys <= UINT32_MAX ? (uint32_t)sys : MESH_PEER_NONE;
    mt_mock *m = mt_mock_start(addr, host, system);
    if (!m) {
        fprintf(stderr, "mesh-top-mock: cannot listen on %s\n", addr);
        return 1;
    }
    /* doom's node 2 stalls, so the node grid's red state is visible. */
    mt_mock_lock(m);
    mt_mock_world *w = mt_mock_world_of(m);
    snprintf(w->stalled[w->nstalled].name, sizeof w->stalled[0].name, "doom");
    w->stalled[w->nstalled++].node = 2;
    mt_mock_unlock(m);
    printf("mock meshd (%s, system %u) on %s -- synthetic data, not a GPU\n", host, system, mt_mock_addr(m));
    fflush(stdout);
    for (;;) pause();
}
