/* @ajax/mesh-cluster-conf: the cluster config (`meshd --cluster FILE --system K`): one key = value
 * file describing every member, shared with the cluster scripts. meshd reads `systems`, every
 * `k.peer` and its own `k.listen`, and the timing keys; other keys (ssh host, dirs, ...) are the
 * scripts' and are ignored here.
 *
 *     systems = 3
 *     0.peer = 192.168.177.11:27800     # what the others dial (system k > 0 listens on it)
 *     1.peer = 192.168.177.11:27801
 *     1.listen = 0.0.0.0:27801          # optional bind address (default: k.peer)
 *     2.peer = 192.168.177.12:27802
 *     dead_ms = 3000                    # optional: a member silent this long is gone
 *     ping_ms = 250                     # optional: the beat period
 *     sched_hold_ms = 6000              # optional: the scheduler's policy (mesh-sched-weights)
 */
#ifndef MESH_CLUSTER_CONF_H
#define MESH_CLUSTER_CONF_H

#include "mesh/abi/base.h"
#include "mesh/sched_weights.h"
#include "mesh/strmap.h"

#include <stdbool.h>
#include <stdint.h>

/* Liveness and election timing, and the scheduler's policy. */
typedef struct {
    uint64_t ping_ms, dead_ms;
    msw_weights sched;
} mcc_tunables;

/* One machine's view of the cluster's links: system k dials every j > k and (k > 0) listens for
 * the systems below it. */
typedef struct {
    uint32_t me, n;
    char *listen; /* where it accepts the lower systems' links; NULL for system 0 */
    size_t ndial;
    uint32_t dial_k[MESH_MAX_SYSTEMS];      /* every j > me ... */
    char *dial_addr[MESH_MAX_SYSTEMS];     /* ... and its address */
    mcc_tunables tun;
} mcc_topology;

mcc_tunables mcc_tunables_default(void);
/* key -> value of a config file (comments `#`, blank lines, `key = value`, values unquoted). */
bool mcc_keys(const char *text, ms_map *out, char **err);
/* System me's topology from the config text. On error *err is the Rust's message. */
bool mcc_topology_parse(const char *text, uint32_t me, mcc_topology *out, char **err);
/* The 2-system shorthands: system 0 dials peer; system 1 listens on addr. */
mcc_topology mcc_pair_dialer(const char *peer);
mcc_topology mcc_pair_listener(const char *addr);
bool mcc_topology_eq(const mcc_topology *a, const mcc_topology *b);
void mcc_topology_free(mcc_topology *t);

#endif
