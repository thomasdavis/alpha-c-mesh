/* @ajax/mesh-top-snap: a parsed `snap` reply in mesh-abi's snapshot layout (crates/mesh-top
 * src/snap.rs): a header, the mailbox header, one section per non-empty program slot (table
 * entry, control lines, manifest extension, node records, view prefix) and the host status block.
 * Accessors take mesh-abi offsets, never literals; reads out of range give 0. The section builder
 * and mt_snapshot_assemble are the inverse, for the mock and the tests. */
#ifndef MESH_TOP_SNAP_H
#define MESH_TOP_SNAP_H

#include "mesh/abi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- little-endian helpers on byte slices (reads out of range give 0) */
uint32_t mt_get_u32(const uint8_t *b, size_t len, size_t at);
uint64_t mt_get_u64(const uint8_t *b, size_t len, size_t at);
float mt_get_f32(const uint8_t *b, size_t len, size_t at);
/* A NUL-padded string field, repaired as String::from_utf8_lossy: out holds at least 3 * n + 1. */
void mt_get_str(const uint8_t *b, size_t len, size_t at, size_t n, char *out);
void mt_put_u32(uint8_t *b, size_t at, uint32_t v);
void mt_put_u64(uint8_t *b, size_t at, uint64_t v);
void mt_put_f32(uint8_t *b, size_t at, float v);
/* put_str: s cut to n - 1 bytes, the rest zeroed. */
void mt_put_str(uint8_t *b, size_t at, size_t n, const char *s);

typedef struct {
    uint32_t sm, heartbeat, tasks, last_op;
    uint64_t busy_ns;
} mt_node;

typedef struct {
    char name[3 * (MESH_FD_NAME_MAX + 1) + 1];
    uint32_t ty;
    size_t off;
} mt_field;

typedef struct {
    uint8_t *bytes;
    size_t len;
} mt_section;

/* ---- the section builder (owned bytes; free with mt_section_free) */
mt_section mt_section_new(uint32_t slot, uint32_t nodes, uint32_t view);
void mt_section_free(mt_section *s);
mt_section mt_section_clone(const mt_section *s);

uint32_t mt_section_slot(const mt_section *s);
size_t mt_section_nodes(const mt_section *s);
uint8_t *mt_section_entry(const mt_section *s); /* MESH_PT_BYTES */
uint8_t *mt_section_ctl(const mt_section *s);   /* MESH_SC_CTL_BYTES */
uint8_t *mt_section_ext(const mt_section *s);   /* MESH_PX_BYTES */
uint8_t *mt_section_node_rec(const mt_section *s, size_t n); /* MESH_NODE_STRIDE */
uint8_t *mt_section_view(const mt_section *s);
size_t mt_section_view_len(const mt_section *s);

/* The name (out: 3 * MESH_NAME_BYTES + 1 bytes). */
void mt_section_name(const mt_section *s, char *out);
bool mt_section_name_is(const mt_section *s, const char *name);
uint32_t mt_section_state(const mt_section *s);
const char *mt_section_state_name(const mt_section *s);
bool mt_section_running(const mt_section *s);
uint32_t mt_section_pt(const mt_section *s, size_t off);
uint64_t mt_section_ptx_hash(const mt_section *s);
uint32_t mt_section_p(const mt_section *s, size_t off);
uint64_t mt_section_p64(const mt_section *s, size_t off);
bool mt_section_native(const mt_section *s);
uint32_t mt_section_counter(const mt_section *s, size_t i);
void mt_section_counter_name(const mt_section *s, size_t i, char *out); /* 3 * MESH_NAME_BYTES + 1 */
/* The manifest fields (at most MESH_MAX_FIELDS); returns the count. */
size_t mt_section_fields(const mt_section *s, mt_field *out);
/* A field's value from the view prefix, as text; "-" when snap_bytes doesn't cover it. */
void mt_section_field_value(const mt_section *s, const mt_field *f, char *out, size_t cap);
mt_node mt_section_node(const mt_section *s, size_t n);
uint32_t mt_section_view_u32(const mt_section *s, size_t off);
float mt_section_view_f32(const mt_section *s, size_t off);

typedef struct {
    uint8_t *bytes;
    size_t len;
    mt_section *sections;
    size_t nsections;
} mt_snapshot;

/* Validates the framing completely (any inconsistency is refused, with *err a message to free).
 * Copies the bytes. */
bool mt_snapshot_parse(const uint8_t *bytes, size_t len, mt_snapshot *out, char **err);
/* Builds a snapshot: head is the mailbox header (MESH_H_BYTES), host the host block (MESH_SNAP_HOST);
 * sections are copied and sorted by slot. */
mt_snapshot mt_snapshot_assemble(const uint8_t *head, const mt_section *sections, size_t n, const uint8_t *host);
mt_snapshot mt_snapshot_clone(const mt_snapshot *s);
void mt_snapshot_free(mt_snapshot *s);

const mt_section *mt_snapshot_by_name(const mt_snapshot *s, const char *name);
const mt_section *mt_snapshot_by_slot(const mt_snapshot *s, uint32_t slot);
uint32_t mt_snapshot_nodes_online(const mt_snapshot *s);
uint32_t mt_snapshot_head(const mt_snapshot *s, size_t off);
const uint8_t *mt_snapshot_host(const mt_snapshot *s);
uint32_t mt_snapshot_host_u32(const mt_snapshot *s, size_t off);
uint64_t mt_snapshot_host_u64(const mt_snapshot *s, size_t off);
float mt_snapshot_host_f32(const mt_snapshot *s, size_t off);
void mt_snapshot_host_str(const mt_snapshot *s, size_t off, size_t n, char *out); /* 3n + 1 */
void mt_snapshot_hostname(const mt_snapshot *s, char out[97]);
void mt_snapshot_gpu(const mt_snapshot *s, char out[97]);
float mt_snapshot_rate(const mt_snapshot *s, uint32_t slot);
/* This machine's system id (false: single). */
bool mt_snapshot_system(const mt_snapshot *s, uint32_t *k);
uint32_t mt_snapshot_systems(const mt_snapshot *s);
uint64_t mt_snapshot_uptime_ms(const mt_snapshot *s);

typedef struct {
    uint32_t system, state, cc, sms, sms_total, blocks_per_sm;
    float rtt_us, wait_us;
    uint32_t rounds;
    uint64_t round;
    char gpu[97], arch[49], host[97];
    uint32_t backend;
} mt_member;
mt_member mt_snapshot_member(const mt_snapshot *s, uint32_t k);
/* Every member record (none when single); returns the count (at most MESH_MAX_SYSTEMS). */
size_t mt_snapshot_members(const mt_snapshot *s, mt_member *out);

typedef struct {
    uint32_t status, group;
    uint64_t applied, heard_ms;
    char reason[121];
} mt_fleet_member;

typedef struct {
    char name[3 * MESH_NAME_BYTES + 1];
    uint32_t epoch, cycle, mask;
    uint8_t order[MESH_MAX_SYSTEMS];
    bool running;
    uint32_t local, rank;
    bool reform;
    uint32_t auto_ms;
} mt_fleet_group;

typedef struct {
    bool has_leader;
    uint32_t leader, role;
    uint64_t term, version, applied;
    uint32_t up, suspect, quorum, flags, dead_ms;
    size_t nmembers;
    mt_fleet_member members[MESH_MAX_SYSTEMS];
    bool has_group;
    mt_fleet_group group;
} mt_fleet;
bool mt_snapshot_fleet(const mt_snapshot *s, mt_fleet *out); /* false when single */

typedef struct {
    uint32_t active, total;
    uint64_t done, failed;
    uint32_t leased;
    float rate;
    uint64_t dupes;
    uint32_t flags, held;
} mt_jobs;
mt_jobs mt_snapshot_jobs(const mt_snapshot *s);

/* A value that may be absent (Rust's Option<f32>). */
typedef struct {
    bool some;
    float v;
} mt_opt_f32;

typedef struct {
    uint64_t at_ms, age_ms;
    uint32_t samples, flags;
    mt_opt_f32 share, share10;
    float slice_us, gap_us, max_gap_us, gaps_s, observed;
    mt_opt_f32 util_ext;
    float queue_us;
    mt_opt_f32 inflate;
    float cpu, cpu_big, cpu_little, iowait;
    float loadavg[3];
    uint32_t cpus, cpus_big, cpus_little;
    bool has_psi;
    float psi[5];
    uint32_t mem_total_mb, mem_avail_mb;
    float mem_free;
    uint32_t swap_total_mb, swap_used_mb;
    float swap_out_s;
    uint32_t compressed_mb;
    bool has_mem_level;
    uint32_t mem_level;
    float meshd_cpu, meshd_rss_mb;
    uint32_t meshd_threads, disk_free_mb, disk_total_mb;
    bool has_thermal;
    uint32_t thermal;
    mt_opt_f32 temp_c;
    size_t ncores;
    uint8_t cores[MESH_MAX_CPUS][2]; /* (busy percent or 0xff, CORE_*) */
} mt_load;
bool mt_snapshot_load(const mt_snapshot *s, mt_load *out); /* false before the first sample */

typedef struct {
    uint32_t flags;
    mt_opt_f32 share, share10;
    float slice_us, gap_us, max_gap_us, observed;
    uint32_t lat_n;
    mt_opt_f32 p50_us, p90_us, p99_us, max_us, task_us;
    float tasks_s, busy, queue_us;
} mt_prog_load;
bool mt_snapshot_prog_load(const mt_snapshot *s, uint32_t slot, mt_prog_load *out);

typedef struct {
    uint64_t age_ms;
    uint32_t flags;
    mt_opt_f32 share, share10;
    float max_gap_us, cpu, cpu_big, cpu_little;
    mt_opt_f32 psi_cpu, psi_mem;
    float meshd_cpu;
    uint32_t mem_avail_mb, mem_total_mb, disk_free_mb, cpus_big, cpus_little;
} mt_member_load;
bool mt_snapshot_member_load(const mt_snapshot *s, uint32_t k, mt_member_load *out);

typedef struct {
    uint32_t system;
    uint64_t rx_bytes, tx_bytes;
    uint64_t rx_frames[MESH_NET_KINDS], tx_frames[MESH_NET_KINDS];
    char local[97], iface[49];
} mt_link_stat;
mt_link_stat mt_snapshot_link(const mt_snapshot *s, uint32_t k);

/* The NI_* counters in this order: rx/tx bytes, rx/tx packets, rx/tx errors, rx/tx drops. */
extern const size_t MT_NIC_COUNTERS[8];

typedef struct {
    char name[49];
    uint64_t v[8];
    uint32_t links, count;
} mt_nic;
/* The last NIC sample: (uptime ms when taken, samples taken). */
void mt_snapshot_net_at(const mt_snapshot *s, uint64_t *at_ms, uint32_t *samples);
size_t mt_snapshot_nics(const mt_snapshot *s, mt_nic *out); /* at most MESH_MAX_NICS */
mt_nic mt_snapshot_net_total(const mt_snapshot *s);
/* The control port's (rx, tx, requests) and the HTTP relay's (rx, tx, connections). */
void mt_snapshot_ports(const mt_snapshot *s, uint64_t ctl[3], uint64_t http[3]);
/* The lockstep program's consistent (name, step, view prefix). */
void mt_snapshot_lock_pair(const mt_snapshot *s, char name[3 * MESH_NAME_BYTES + 1], uint64_t *step, uint8_t view[32]);

/* The GPU column: "12.1", "apple9", "metal". out holds 32. */
void mt_gpu_label(uint32_t cc, uint32_t backend, char *out);
void mt_blocks_label(uint32_t blocks, uint32_t backend, char *out);
void mt_cc_label(uint32_t cc, char *out);

typedef struct {
    uint32_t slot, node, op, ticket, start, end;
} mt_rec;
static inline uint32_t mt_rec_ns(const mt_rec *r) { return r->end - r->start; }

typedef struct {
    uint32_t slot, n;
    mt_rec *recs; /* malloc'd, oldest first */
    size_t nrecs;
} mt_trace_prog;
/* A `trace` reply: per non-empty slot, u32 slot, u32 P_TRACE_N, then its ring. *out is malloc'd
 * (free with mt_trace_free). False with *err (to free) on a bad reply. */
bool mt_parse_trace(const uint8_t *b, size_t len, mt_trace_prog **out, size_t *n, char **err);
void mt_trace_free(mt_trace_prog *t, size_t n);
/* The q-quantile (0..=1) of task durations in µs; false without records. */
bool mt_quantile_us(const mt_rec *recs, size_t n, double q, double *out);

#endif
