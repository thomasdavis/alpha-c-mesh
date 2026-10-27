/* @ajax/mesh-top-mock: a tiny mock of meshd's control port for mesh-top's tests and demo
 * (crates/mesh-top tests/support/mock.rs). It is NOT meshd: the snapshot, trace and log are
 * synthesised on the CPU (a toy raycaster, a scripted loss curve, a program table with a fake
 * catalog) purely to exercise the viewer. It speaks mesh-abi's control protocol. Time comes from
 * mesh-top-clock, so a test on the virtual clock gets the same bytes every run. */
#ifndef MESH_TOP_MOCK_H
#define MESH_TOP_MOCK_H

#include "mesh/top_clock.h"
#include "mesh/top_snap.h"
#include "mesh/top_views.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t type;
    size_t off;
} mt_mock_field;

/* One loaded program, as the mock's table keeps it. */
typedef struct {
    uint32_t slot;
    char name[32];
    uint32_t state, nodes, view_bytes, snap_bytes, policy, hz, fanout, peer;
    const char *counters[4];
    mt_mock_field fields[8];
    size_t nfields;
    uint64_t hash;
    bool has_retained;
    uint64_t retained;
    uint32_t regs, loads;
    float rate;      /* tasks/s */
    uint32_t task_ns;
    int64_t offset;  /* completions: offset + elapsed * rate, or frozen while stopped */
    bool has_frozen;
    uint32_t frozen;
    bool has_native_pid;
    uint32_t native_pid;
} mt_mock_prog;

#define MT_MOCK_CATALOG_LEN 7
extern const char *const MT_MOCK_CATALOG[MT_MOCK_CATALOG_LEN];
/* The manifest-equivalent of a catalog entry (false: not in the catalog). */
bool mt_mock_catalog_prog(const char *name, uint32_t slot, mt_mock_prog *out);

typedef struct {
    char host[64];
    uint32_t system;
    mt_mock_prog progs[MESH_PROG_SLOTS];
    size_t nprogs;
    uint32_t epoch, prog_seq;
    float pos[2], dir[2];
    mt_instant step0;
    bool has_frozen_step;
    uint32_t frozen_step;
    mt_instant started;
    uint64_t snaps;
    uint32_t http_served;
    char last_http[96];
    struct {
        char name[32];
        size_t node;
    } stalled[8];
    size_t nstalled;
    char **log;
    size_t nlog;
    uint32_t hello_last[2];
    uint32_t keys;
    uint32_t place[2]; /* (PLACE_*, system) */
    uint8_t assign[MESH_PLACE_PARTS_MAX];
    uint32_t n;
    char gpu[64];
    uint32_t cc, blocks_per_sm;
    char arch[32];
    float waits[MESH_MAX_SYSTEMS];
    uint32_t down[MESH_MAX_SYSTEMS];
    size_t ndown;
    bool has_clock_ms;
    uint64_t clock_ms; /* meshd's uptime (the network counters' clock); else real time since started */
} mt_mock_world;

/* doom, train, infer, http, hello in slots 0..4, all running. */
void mt_mock_world_init(mt_mock_world *w, const char *host, uint32_t system);
void mt_mock_world_free(mt_mock_world *w);
mt_mock_prog *mt_mock_prog_named(mt_mock_world *w, const char *name);
void mt_mock_default_assign(uint32_t n, uint32_t fanout, uint8_t out[MESH_PLACE_PARTS_MAX]);
uint32_t mt_mock_train_step(const mt_mock_world *w);
void mt_mock_params_at(uint32_t step, float out[4]);

/* The snapshot meshd would send now (it counts as a snapshot taken). */
mt_snapshot mt_mock_synth(mt_mock_world *w);
/* The `trace` reply (malloc'd). */
uint8_t *mt_mock_trace(const mt_mock_world *w, size_t *len);
/* One control line: a framed reply (*bytes set, malloc'd) or a text one (*text, malloc'd). */
void mt_mock_request(mt_mock_world *w, const char *req, uint8_t **bytes, size_t *len, char **text);

#define MT_MOCK_ROUNDS_PER_S 2000u
#define MT_MOCK_ROUND_BYTES (1u + 12u + (uint64_t)MESH_EX_REGION_BYTES)
const char *mt_mock_iface(uint32_t a, uint32_t b);

/* The control-port server: a thread accepting connections, one thread each. */
typedef struct mt_mock mt_mock;
/* Listens on addr ("127.0.0.1:0" picks a port); NULL when it can't. */
mt_mock *mt_mock_start(const char *addr, const char *host, uint32_t system);
/* On an existing world (shared: the reconnect test reuses one); the world outlives the mock. */
mt_mock *mt_mock_start_with(const char *addr, mt_mock_world *w);
const char *mt_mock_addr(const mt_mock *m);
mt_mock_world *mt_mock_world_of(mt_mock *m);
void mt_mock_lock(mt_mock *m);
void mt_mock_unlock(mt_mock *m);
/* Whether a request line was received (the requests are logged). */
bool mt_mock_saw(mt_mock *m, const char *req);
/* Closes the listener and every connection, then frees the mock (not a shared world). */
void mt_mock_stop(mt_mock *m);

#endif
