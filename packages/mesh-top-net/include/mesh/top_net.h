/* @ajax/mesh-top-net: network rates from meshd's cumulative counters (crates/mesh-top src/net.rs).
 * Rates are taken on meshd's own clock, never the viewer's: the live counters (links, control
 * port, HTTP relay) against the snapshot's uptime, the NIC counters against their sample's time,
 * so polling jitter and the once-a-second NIC sampling don't show as noise. */
#ifndef MESH_TOP_NET_H
#define MESH_TOP_NET_H

#include "mesh/top_snap.h"

#define MT_NET_WINDOW_MS 1000u
#define MT_NET_HISTORY 120u

/* One cumulative counter sampled at meshd times (ms), and its rate over about a second. */
typedef struct {
    uint64_t (*s)[2]; /* (t, v) samples, oldest first */
    size_t n, cap;
} mt_meter;

void mt_meter_push(mt_meter *m, uint64_t t, uint64_t v);
double mt_meter_per_s(const mt_meter *m);
void mt_meter_free(mt_meter *m);

typedef struct {
    mt_meter rx, tx, rx_frames[MESH_NET_KINDS], tx_frames[MESH_NET_KINDS];
} mt_link_track;
void mt_link_frames_per_s(const mt_link_track *l, size_t kind, double *rx, double *tx);

typedef struct {
    mt_meter v[8]; /* MT_NIC_COUNTERS order */
} mt_nic_track;
double mt_nic_rx(const mt_nic_track *t);
double mt_nic_tx(const mt_nic_track *t);
void mt_nic_packets(const mt_nic_track *t, double *rx, double *tx);
double mt_nic_errors(const mt_nic_track *t);
double mt_nic_drops(const mt_nic_track *t);

typedef struct {
    char name[49];
    mt_nic_track t;
} mt_named_nic;

typedef struct {
    uint64_t uptime, nic_at;
    bool has_links;
    mt_link_track links[MESH_MAX_SYSTEMS];
    mt_named_nic *nics; /* sorted by name, as a BTreeMap */
    size_t nnics;
    mt_nic_track total;
    mt_meter ctl[3], http[3];
    uint64_t *hist, *mesh_hist; /* rings of MT_NET_HISTORY, oldest first */
    size_t nhist;
} mt_net_track;

void mt_net_update(mt_net_track *n, const mt_snapshot *s);
void mt_net_free(mt_net_track *n);
bool mt_net_nic_age_ms(const mt_net_track *n, uint64_t *age);
const mt_link_track *mt_net_link(const mt_net_track *n, uint32_t k); /* NULL before a snapshot */
const mt_nic_track *mt_net_nic(const mt_net_track *n, const char *name);
double mt_net_mesh_bytes(const mt_net_track *n);
bool mt_net_mesh_share(const mt_net_track *n, double *share);

/* Bytes per second in decimal units: "870 B/s", "12.3 kB/s", "1.2 MB/s". */
void mt_human(double bps, char *out, size_t cap);
/* The same, short: "870", "12.3k", "154k", "1.2M". */
void mt_short(double x, char *out, size_t cap);
/* A text sparkline of the last width values, scaled to their maximum, padded left. out holds
 * 3 * width + 1. */
void mt_spark(const uint64_t *v, size_t n, size_t width, char *out);

#endif
