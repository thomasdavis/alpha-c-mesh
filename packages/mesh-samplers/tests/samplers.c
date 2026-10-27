/* Both samplers on this machine for a few seconds: the host block fills (load samples, CPUs, memory,
 * a program's record; NIC samples and the total), then they stop on quit. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "mesh/check.h"
#include "mesh/log.h"
#include "mesh/samplers.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static mesh_shared *SH;
static void *load(void *a) { (void)a; mesh_load_sampler(SH); return NULL; }
static void *net(void *a) { (void)a; mesh_net_sampler(SH); return NULL; }

int main(void) {
    mesh_log_set_fd(-1);
    static _Atomic bool quit;
    mesh_mailbox mb = mesh_mailbox_test();
    SH = mesh_shared_new(mb, ".", false, 0, 1, &quit);
    mesh_mailbox_set(mb, mesh_prog_entry(1) + MESH_PT_STATE, MESH_ST_RUNNING);
    mesh_mailbox_set(mb, mesh_prog_entry(1) + MESH_PT_NODES, 1);
    mesh_mailbox_set(mb, mesh_prog_entry(1) + MESH_PT_LOADS, 1);
    mesh_mailbox_set(mb, mesh_prog_ext(1) + MESH_PX_EXEC, MESH_BACKEND_WASM);
    pthread_t a, b;
    pthread_create(&a, NULL, load, NULL);
    pthread_create(&b, NULL, net, NULL);
    mesh_sleep_us(2600000);
    atomic_store(&quit, true);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    uint8_t *h = malloc(MESH_SNAP_HOST);
    mesh_host_block_bytes(&SH->host, h);
    uint32_t samples, cpus, mem, nets, flags, pflags;
    memcpy(&samples, h + MESH_S_LOAD_SAMPLES, 4);
    memcpy(&cpus, h + MESH_S_CPUS, 4);
    memcpy(&mem, h + MESH_S_MEM_TOTAL_MB, 4);
    memcpy(&nets, h + MESH_S_NET_SAMPLES, 4);
    memcpy(&flags, h + MESH_S_NET_TOTAL + MESH_NI_COUNT, 4);
    memcpy(&pflags, h + mesh_pload(1) + MESH_PL_FLAGS, 4);
    CHECK(samples >= 1);
    CHECK(cpus >= 1 && mem > 0);
    CHECK(nets >= 2);
    CHECK(memcmp(h + MESH_S_NET_TOTAL, "total", 5) == 0);
    CHECK(pflags & MESH_PLF_LOADED);
    CHECK(pflags & MESH_PLF_HOST);
    CHECK(mesh_mailbox_get(mb, MESH_H_LOAD_WIN) >= 1);
    /* The load page mirrors the machine region. */
    uint8_t lp[16];
    mesh_mailbox_read(mb, MESH_LOAD_PAGE, lp, 16);
    CHECK(memcmp(lp, h + MESH_S_LOAD, 16) == 0);
    free(h);
    mesh_shared_free(SH);
    free(mb.base);
    return mesh_check_done();
}
