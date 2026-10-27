/* fleet.rs's test network, ported: a deterministic network of n cores -- messages delayed, dropped,
 * partitions, crashes and restarts, disks that fill up -- that asserts at most one leader per term,
 * one document per committed version and per accepted (term, version), no committed change lost,
 * and no member's committed document going back. */
#ifndef SIM_H
#define SIM_H

#include "mesh/check.h"
#include "mesh/fleet_core.h"
#include "mesh/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t at;
    uint32_t from, to;
    mfc_msg msg;
} wire_t;

typedef struct {
    uint32_t n;
    mfc_core *cores[8];
    mfp_persist disk[8];
    uint64_t now;
    wire_t *wire;
    size_t nwire, capwire;
    uint32_t side[8];
    uint64_t rng, loss;
    uint64_t leader_term[4096];
    uint32_t leader_of[4096];
    size_t nleaders;
    mfd_doc *committed; /* by version, in insertion order */
    size_t ncommitted;
    mfd_doc *accepted;
    size_t naccepted;
    mfc_id high[8];
    bool bad_disk[8];
    uint32_t fail_stops;
    int failed; /* an invariant broke */
} sim_t;

static uint64_t sim_rand(sim_t *s, uint64_t n) {
    s->rng ^= s->rng << 13;
    s->rng ^= s->rng >> 7;
    s->rng ^= s->rng << 17;
    return s->rng % (n ? n : 1);
}

static mfc_core *sim_boot(sim_t *s, uint32_t k) {
    uint64_t seed = sim_rand(s, 1u << 30) + 1;
    return mfc_new((mfc_config){k, s->n, 3000, seed}, mfp_clone(&s->disk[k]), s->now);
}

static bool sim_connected(sim_t *s, uint32_t a, uint32_t b) { return s->cores[a] && s->cores[b] && s->side[a] == s->side[b]; }

static void sim_crash(sim_t *s, uint32_t k);

typedef struct {
    sim_t *s;
    uint32_t k;
} persist_ctx;

static bool sim_persist(void *ctx, char **err) {
    persist_ctx *p = ctx;
    if (p->s->bad_disk[p->k]) {
        *err = mt_strdup("injected: no space left on device");
        return false;
    }
    mfp_free(&p->s->disk[p->k]);
    p->s->disk[p->k] = mfp_clone(mfc_persist(p->s->cores[p->k]));
    return true;
}

static mfd_doc *find_version(sim_t *s, uint64_t v) {
    for (size_t i = 0; i < s->ncommitted; i++)
        if (s->committed[i].version == v) return &s->committed[i];
    return NULL;
}

static void sim_committed(sim_t *s, const mfd_doc *d) {
    mfd_doc *o = find_version(s, d->version);
    if (o) {
        if (!mfd_doc_eq(o, d)) {
            fprintf(stderr, "two different documents committed as version %llu\n", (unsigned long long)d->version);
            s->failed++;
        }
        return;
    }
    /* No committed version is lost: every change (a tag) of a lower committed version is in this one,
     * and this one's are in every higher one. */
    for (size_t i = 0; i < s->ncommitted; i++) {
        const mfd_doc *x = &s->committed[i];
        const mfd_doc *lo = x->version < d->version ? x : d, *hi = x->version < d->version ? d : x;
        for (size_t t = 0; t < lo->nplaces; t++)
            if (!mfd_place_get(hi, lo->place_name[t])) {
                fprintf(stderr, "committed version %llu lost %s of committed version %llu\n", (unsigned long long)hi->version, lo->place_name[t],
                        (unsigned long long)lo->version);
                s->failed++;
            }
    }
    s->committed = mt_realloc(s->committed, (s->ncommitted + 1) * sizeof *s->committed);
    s->committed[s->ncommitted++] = mfd_doc_clone(d);
}

static void sim_apply(sim_t *s, uint32_t k, mfc_outs out) {
    persist_ctx pc = {s, k};
    char *err = NULL;
    if (!mfc_durable(&out, sim_persist, &pc, &err)) {
        free(err);
        mfc_outs_free(&out);
        s->fail_stops++;
        sim_crash(s, k);
        return;
    }
    for (size_t i = 0; i < out.n; i++) {
        mfc_out *o = &out.v[i];
        if (o->kind == MFC_OUT_SEND) {
            if (sim_rand(s, 100) < s->loss || !sim_connected(s, k, o->to)) continue;
            uint64_t at = s->now + 1 + sim_rand(s, 40);
            if (s->nwire == s->capwire) {
                s->capwire = s->capwire ? s->capwire * 2 : 64;
                s->wire = mt_realloc(s->wire, s->capwire * sizeof *s->wire);
            }
            s->wire[s->nwire++] = (wire_t){at, k, o->to, o->msg};
            o->msg = (mfc_msg){0}; /* moved */
        } else if (o->kind == MFC_OUT_COMMITTED) {
            sim_committed(s, mfc_committed(s->cores[k]));
        } else if (o->kind == MFC_OUT_LEADER && o->has_leader && o->leader == k) {
            uint64_t t = mfc_term(s->cores[k]);
            for (size_t j = 0; j < s->nleaders; j++)
                if (s->leader_term[j] == t && s->leader_of[j] != k) {
                    fprintf(stderr, "two leaders in term %llu: %u and %u\n", (unsigned long long)t, s->leader_of[j], k);
                    s->failed++;
                }
            s->leader_term[s->nleaders] = t;
            s->leader_of[s->nleaders++] = k;
        }
    }
    mfc_outs_free(&out);
    if (s->cores[k]) {
        /* A member's committed document never goes back (nor across its restarts). */
        mfc_id id = mfc_doc_id(mfc_committed(s->cores[k]));
        if (mfc_id_cmp(id, s->high[k]) < 0) {
            fprintf(stderr, "member %u: committed went back\n", k);
            s->failed++;
        }
        s->high[k] = id;
        const mfd_doc *a = mfc_accepted(s->cores[k]);
        if (a->version > 0) {
            bool seen = false;
            for (size_t i = 0; i < s->naccepted; i++)
                if (s->accepted[i].term == a->term && s->accepted[i].version == a->version) {
                    seen = true;
                    if (!mfd_doc_eq(&s->accepted[i], a)) {
                        fprintf(stderr, "two documents accepted as (%llu, %llu)\n", (unsigned long long)a->term, (unsigned long long)a->version);
                        s->failed++;
                    }
                }
            if (!seen) {
                s->accepted = mt_realloc(s->accepted, (s->naccepted + 1) * sizeof *s->accepted);
                s->accepted[s->naccepted++] = mfd_doc_clone(a);
            }
        }
    }
}

static void sim_relink(sim_t *s) {
    for (uint32_t a = 0; a < s->n; a++)
        for (uint32_t b = 0; b < s->n; b++) {
            if (a == b || !s->cores[a]) continue;
            bool up = sim_connected(s, a, b);
            bool was = mfc_linked(s->cores[a]) >> b & 1;
            if (up != was) sim_apply(s, a, mfc_link(s->cores[a], b, up, s->now));
        }
}

static void sim_crash(sim_t *s, uint32_t k) {
    if (!s->cores[k]) return;
    mfc_free(s->cores[k]);
    s->cores[k] = NULL;
    sim_relink(s);
}

static void sim_restart(sim_t *s, uint32_t k) {
    s->bad_disk[k] = false;
    s->cores[k] = sim_boot(s, k);
    sim_relink(s);
}

static void sim_init(sim_t *s, uint32_t n, uint64_t seed, uint64_t loss) {
    memset(s, 0, sizeof *s);
    s->n = n;
    s->now = 1;
    s->rng = seed | 1;
    s->loss = loss;
    for (uint32_t k = 0; k < n; k++) s->cores[k] = sim_boot(s, k);
    sim_relink(s);
}

static void sim_free(sim_t *s) {
    for (uint32_t k = 0; k < s->n; k++) {
        mfc_free(s->cores[k]);
        mfp_free(&s->disk[k]);
    }
    for (size_t i = 0; i < s->nwire; i++) mfc_msg_free(&s->wire[i].msg);
    free(s->wire);
    for (size_t i = 0; i < s->ncommitted; i++) mfd_doc_free(&s->committed[i]);
    free(s->committed);
    for (size_t i = 0; i < s->naccepted; i++) mfd_doc_free(&s->accepted[i]);
    free(s->accepted);
}

/* peer.rs `lead`: a leader whose term has no committed document proposes its first one. */
static void sim_lead(sim_t *s, uint32_t k) {
    mfc_core *c = s->cores[k];
    if (!c || mfc_role_of(c) != MFC_LEADER || mfc_busy(c) || mfc_term_committed(c)) return;
    mfd_doc d = {0};
    mfc_first_doc(c, &d);
    uint64_t v;
    mfc_outs out;
    char *err = NULL;
    if (mfc_propose(c, d, s->now, &v, &out, &err)) sim_apply(s, k, out);
    else free(err);
}

static void sim_step(sim_t *s, uint64_t ms) {
    uint64_t end = s->now + ms;
    while (s->now < end) {
        s->now++;
        /* Deliver what is due, in order (links that broke meanwhile lose it). */
        size_t nd = 0;
        wire_t *due = NULL;
        size_t j = 0;
        for (size_t i = 0; i < s->nwire; i++) {
            if (s->wire[i].at <= s->now) {
                due = mt_realloc(due, (nd + 1) * sizeof *due);
                due[nd++] = s->wire[i];
            } else s->wire[j++] = s->wire[i];
        }
        s->nwire = j;
        for (size_t i = 0; i < nd; i++) {
            if (!sim_connected(s, due[i].from, due[i].to)) {
                mfc_msg_free(&due[i].msg);
                continue;
            }
            sim_apply(s, due[i].to, mfc_recv(s->cores[due[i].to], due[i].from, due[i].msg, s->now));
        }
        free(due);
        if (s->now % 25 == 0) {
            for (uint32_t k = 0; k < s->n; k++) {
                mfc_core *c = s->cores[k];
                if (!c) continue;
                mfc_outs out = mfc_tick(c, s->now);
                if (s->now % 250 == 0) {
                    mfc_msg b = mfc_beat(c);
                    for (uint32_t t = 0; t < s->n; t++)
                        if (t != k) {
                            if (out.n == out.cap) {
                                out.cap = out.cap ? out.cap * 2 : 8;
                                out.v = mt_realloc(out.v, out.cap * sizeof *out.v);
                            }
                            out.v[out.n++] = (mfc_out){.kind = MFC_OUT_SEND, .to = t, .msg = b};
                        }
                }
                sim_apply(s, k, out);
                sim_lead(s, k);
            }
        }
    }
}

/* The single leader, or -1. */
static int sim_leader(sim_t *s) {
    int l = -1, count = 0;
    for (uint32_t k = 0; k < s->n; k++)
        if (s->cores[k] && mfc_role_of(s->cores[k]) == MFC_LEADER) l = (int)k, count++;
    return count == 1 ? l : -1;
}

/* A change built on the committed document, once this term committed one: version, or 0 with *err. */
static uint64_t sim_propose(sim_t *s, uint32_t k, const char *tag, char **err) {
    mfc_core *c = s->cores[k];
    if (mfc_role_of(c) == MFC_LEADER && !mfc_term_committed(c)) {
        *err = mt_strdup("the term's first document is not committed yet");
        return 0;
    }
    mfd_doc d = mfd_doc_clone(mfc_committed(c));
    mt_strs w = {0};
    mt_push(&w, "local");
    mfd_place_put(&d, tag, w);
    uint64_t v;
    mfc_outs out;
    if (!mfc_propose(c, d, s->now, &v, &out, err)) return 0;
    sim_apply(s, k, out);
    return v;
}

#endif
