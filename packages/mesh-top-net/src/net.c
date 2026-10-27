/* crates/mesh-top/src/net.rs, ported. */
#include "mesh/top_net.h"

#include "rsfmt.h"

#include <stdio.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BACKWARDS ((uint64_t)1 << 62)

void mt_meter_push(mt_meter *m, uint64_t t, uint64_t v) {
    if (m->n) {
        uint64_t lt = m->s[m->n - 1][0], lv = m->s[m->n - 1][1];
        if (t < lt || v - lv >= BACKWARDS) m->n = 0; /* a restart or a reset: start over */
        else if (t == lt) m->n--;                    /* the same instant again: keep the newer value */
    }
    if (m->n == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 8;
        m->s = realloc(m->s, m->cap * sizeof *m->s);
        if (!m->s) abort();
    }
    m->s[m->n][0] = t;
    m->s[m->n][1] = v;
    m->n++;
    while (m->n > 2 && t - m->s[1][0] >= MT_NET_WINDOW_MS) {
        memmove(m->s, m->s + 1, (m->n - 1) * sizeof *m->s);
        m->n--;
    }
}

double mt_meter_per_s(const mt_meter *m) {
    if (m->n == 0) return 0.0;
    uint64_t at = m->s[0][0], av = m->s[0][1], bt = m->s[m->n - 1][0], bv = m->s[m->n - 1][1];
    if (bt <= at) return 0.0;
    return (double)(bv - av) * 1000.0 / (double)(bt - at);
}

void mt_meter_free(mt_meter *m) {
    free(m->s);
    *m = (mt_meter){NULL, 0, 0};
}

void mt_link_frames_per_s(const mt_link_track *l, size_t kind, double *rx, double *tx) {
    *rx = mt_meter_per_s(&l->rx_frames[kind]);
    *tx = mt_meter_per_s(&l->tx_frames[kind]);
}

static void nic_push(mt_nic_track *t, uint64_t at, const uint64_t v[8]) {
    for (int i = 0; i < 8; i++) mt_meter_push(&t->v[i], at, v[i]);
}
static void nic_free(mt_nic_track *t) {
    for (int i = 0; i < 8; i++) mt_meter_free(&t->v[i]);
}
double mt_nic_rx(const mt_nic_track *t) { return mt_meter_per_s(&t->v[0]); }
double mt_nic_tx(const mt_nic_track *t) { return mt_meter_per_s(&t->v[1]); }
void mt_nic_packets(const mt_nic_track *t, double *rx, double *tx) {
    *rx = mt_meter_per_s(&t->v[2]);
    *tx = mt_meter_per_s(&t->v[3]);
}
double mt_nic_errors(const mt_nic_track *t) { return mt_meter_per_s(&t->v[4]) + mt_meter_per_s(&t->v[5]); }
double mt_nic_drops(const mt_nic_track *t) { return mt_meter_per_s(&t->v[6]) + mt_meter_per_s(&t->v[7]); }

static void link_free(mt_link_track *l) {
    mt_meter_free(&l->rx);
    mt_meter_free(&l->tx);
    for (size_t i = 0; i < MESH_NET_KINDS; i++) {
        mt_meter_free(&l->rx_frames[i]);
        mt_meter_free(&l->tx_frames[i]);
    }
}

void mt_net_free(mt_net_track *n) {
    for (size_t k = 0; k < MESH_MAX_SYSTEMS; k++) link_free(&n->links[k]);
    for (size_t i = 0; i < n->nnics; i++) nic_free(&n->nics[i].t);
    free(n->nics);
    nic_free(&n->total);
    for (int i = 0; i < 3; i++) {
        mt_meter_free(&n->ctl[i]);
        mt_meter_free(&n->http[i]);
    }
    free(n->hist);
    free(n->mesh_hist);
    memset(n, 0, sizeof *n);
}

void mt_net_update(mt_net_track *n, const mt_snapshot *s) {
    uint64_t t = mt_snapshot_uptime_ms(s);
    if (t < n->uptime) mt_net_free(n); /* meshd restarted: every count starts over */
    n->uptime = t;
    n->has_links = true;
    for (uint32_t k = 0; k < MESH_MAX_SYSTEMS; k++) {
        mt_link_stat l = mt_snapshot_link(s, k);
        mt_link_track *lt = &n->links[k];
        mt_meter_push(&lt->rx, t, l.rx_bytes);
        mt_meter_push(&lt->tx, t, l.tx_bytes);
        for (size_t i = 0; i < MESH_NET_KINDS; i++) {
            mt_meter_push(&lt->rx_frames[i], t, l.rx_frames[i]);
            mt_meter_push(&lt->tx_frames[i], t, l.tx_frames[i]);
        }
    }
    uint64_t ctl[3], http[3];
    mt_snapshot_ports(s, ctl, http);
    for (int i = 0; i < 3; i++) {
        mt_meter_push(&n->ctl[i], t, ctl[i]);
        mt_meter_push(&n->http[i], t, http[i]);
    }
    uint64_t at;
    uint32_t samples;
    mt_snapshot_net_at(s, &at, &samples);
    if (at > 0 && at != n->nic_at) {
        n->nic_at = at;
        mt_nic total = mt_snapshot_net_total(s);
        nic_push(&n->total, at, total.v);
        mt_nic nics[MESH_MAX_NICS];
        size_t nn = mt_snapshot_nics(s, nics);
        /* Keep only the NICs still reported. */
        size_t w = 0;
        for (size_t i = 0; i < n->nnics; i++) {
            bool keep = false;
            for (size_t j = 0; j < nn; j++) keep |= strcmp(nics[j].name, n->nics[i].name) == 0;
            if (keep) n->nics[w++] = n->nics[i];
            else nic_free(&n->nics[i].t);
        }
        n->nnics = w;
        for (size_t j = 0; j < nn; j++) {
            size_t i = 0;
            while (i < n->nnics && strcmp(n->nics[i].name, nics[j].name) < 0) i++;
            if (i == n->nnics || strcmp(n->nics[i].name, nics[j].name) != 0) {
                n->nics = realloc(n->nics, (n->nnics + 1) * sizeof *n->nics);
                if (!n->nics) abort();
                memmove(&n->nics[i + 1], &n->nics[i], (n->nnics - i) * sizeof *n->nics);
                memset(&n->nics[i], 0, sizeof n->nics[i]);
                strcpy(n->nics[i].name, nics[j].name);
                n->nnics++;
            }
            nic_push(&n->nics[i].t, at, nics[j].v);
        }
        if (n->total.v[0].n >= 2) {
            if (!n->hist) {
                n->hist = calloc(MT_NET_HISTORY, sizeof *n->hist);
                n->mesh_hist = calloc(MT_NET_HISTORY, sizeof *n->mesh_hist);
                if (!n->hist || !n->mesh_hist) abort();
            }
            if (n->nhist == MT_NET_HISTORY) {
                memmove(n->hist, n->hist + 1, (MT_NET_HISTORY - 1) * sizeof *n->hist);
                memmove(n->mesh_hist, n->mesh_hist + 1, (MT_NET_HISTORY - 1) * sizeof *n->mesh_hist);
                n->nhist--;
            }
            double sum = mt_nic_rx(&n->total) + mt_nic_tx(&n->total), mesh = mt_net_mesh_bytes(n);
            n->hist[n->nhist] = sum > 0 ? (sum >= 1.8446744073709552e19 ? UINT64_MAX : (uint64_t)sum) : 0;
            n->mesh_hist[n->nhist] = mesh > 0 ? (mesh >= 1.8446744073709552e19 ? UINT64_MAX : (uint64_t)mesh) : 0;
            n->nhist++;
        }
    }
}

bool mt_net_nic_age_ms(const mt_net_track *n, uint64_t *age) {
    if (n->nic_at == 0) return false;
    *age = n->uptime > n->nic_at ? n->uptime - n->nic_at : 0;
    return true;
}

const mt_link_track *mt_net_link(const mt_net_track *n, uint32_t k) {
    if (!n->has_links || k >= MESH_MAX_SYSTEMS) return NULL;
    return &n->links[k];
}

const mt_nic_track *mt_net_nic(const mt_net_track *n, const char *name) {
    for (size_t i = 0; i < n->nnics; i++)
        if (strcmp(n->nics[i].name, name) == 0) return &n->nics[i].t;
    return NULL;
}

double mt_net_mesh_bytes(const mt_net_track *n) {
    double s = 0;
    if (!n->has_links) return 0;
    for (size_t k = 0; k < MESH_MAX_SYSTEMS; k++) s += mt_meter_per_s(&n->links[k].rx) + mt_meter_per_s(&n->links[k].tx);
    return s;
}

bool mt_net_mesh_share(const mt_net_track *n, double *share) {
    double t = mt_nic_rx(&n->total) + mt_nic_tx(&n->total);
    if (!(t > 0.0)) return false;
    double x = mt_net_mesh_bytes(n) / t;
    *share = x < 1.0 ? x : 1.0;
    return true;
}

static double scale(double x, const char **u) {
    x = isfinite(x) ? (x > 0.0 ? x : 0.0) : 0.0;
    if (x >= 1e9) {
        *u = "G";
        return x / 1e9;
    }
    if (x >= 1e6) {
        *u = "M";
        return x / 1e6;
    }
    if (x >= 1e3) {
        *u = "k";
        return x / 1e3;
    }
    *u = "";
    return x;
}

void mt_human(double bps, char *out, size_t cap) {
    const char *u;
    double v = scale(bps, &u);
    char num[64];
    rsfmt_fixed(num, sizeof num, v, *u ? 1 : 0, 0);
    if (*u) snprintf(out, cap, "%s %sB/s", num, u);
    else snprintf(out, cap, "%s B/s", num);
}

void mt_short(double x, char *out, size_t cap) {
    const char *u;
    double v = scale(x, &u);
    char num[64];
    rsfmt_fixed(num, sizeof num, v, !*u || v >= 100.0 ? 0 : 1, 0);
    snprintf(out, cap, "%s%s", num, u);
}

void mt_spark(const uint64_t *v, size_t n, size_t width, char *out) {
    static const char *const BARS[8] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    size_t skip = n > width ? n - width : 0, shown = n - skip;
    uint64_t max = 0;
    for (size_t i = skip; i < n; i++)
        if (v[i] > max) max = v[i];
    if (max < 1) max = 1;
    size_t k = 0;
    for (size_t i = shown; i < width; i++) out[k++] = ' ';
    for (size_t i = skip; i < n; i++) {
        size_t b = (size_t)round((double)v[i] / (double)max * 7.0);
        memcpy(out + k, BARS[b], 3);
        k += 3;
    }
    out[k] = 0;
}
