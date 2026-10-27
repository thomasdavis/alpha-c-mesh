/* fleet.rs Core: the election and commit state machine (see the header). */
#include "mesh/fleet_core.h"
#include "mesh/member_list.h"
#include "mesh/mt.h"

#include <stdlib.h>
#include <string.h>

mfc_msg mfc_msg_clone(const mfc_msg *m) {
    mfc_msg c = *m;
    if (m->kind == MFC_STATE) c.doc = mfd_doc_clone(&m->doc);
    return c;
}

void mfc_msg_free(mfc_msg *m) {
    if (m->kind == MFC_STATE) mfd_doc_free(&m->doc);
}

const char *mfc_role_word(mfc_role r) {
    static const char *const w[] = {"follower", "pre-candidate", "candidate", "leader"};
    return w[r];
}

uint32_t mfc_role_code(mfc_role r) { return (uint32_t)r + 1; }

bool mfc_fixed(const mfc_config *c) { return c->n == 2; }
uint32_t mfc_quorum(const mfc_config *c) { return mfc_fixed(c) ? 1 : c->n / 2 + 1; }

static void out_free(mfc_out *o) {
    if (o->kind == MFC_OUT_SEND) mfc_msg_free(&o->msg);
    free(o->text);
}

void mfc_outs_free(mfc_outs *o) {
    for (size_t i = 0; i < o->n; i++) out_free(&o->v[i]);
    free(o->v);
    *o = (mfc_outs){0};
}

static void push(mfc_outs *o, mfc_out x) {
    if (o->n == o->cap) {
        o->cap = o->cap ? o->cap * 2 : 8;
        o->v = mt_realloc(o->v, o->cap * sizeof *o->v);
    }
    o->v[o->n++] = x;
}

bool mfc_durable(mfc_outs *outs, mfc_persist_fn persist, void *ctx, char **err) {
    size_t j = 0;
    for (size_t i = 0; i < outs->n; i++) {
        if (outs->v[i].kind != MFC_OUT_PERSIST) {
            outs->v[j++] = outs->v[i];
            continue;
        }
        if (!persist(ctx, err)) {
            for (size_t k = i + 1; k < outs->n; k++) out_free(&outs->v[k]);
            outs->n = j;
            return false;
        }
    }
    outs->n = j;
    return true;
}

typedef struct {
    bool on;
    mfd_doc doc;
    uint32_t acks;
    uint64_t deadline;
} inflight;

struct mfc_core {
    mfc_config cfg;
    mfp_persist p;
    mfc_role role;
    bool has_leader;
    uint32_t leader;
    uint32_t linked;
    uint64_t last_heard[MESH_MAX_SYSTEMS];
    uint64_t leader_contact;
    uint64_t election_at;
    uint32_t votes;
    uint64_t rng;
    inflight inflight;
    uint64_t caught_up_at[MESH_MAX_SYSTEMS]; /* catch-up sends per member, at most one a second */
};

static uint64_t rnd(mfc_core *c, uint64_t n) {
    c->rng ^= c->rng << 13;
    c->rng ^= c->rng >> 7;
    c->rng ^= c->rng << 17;
    return n == 0 ? 0 : c->rng % n;
}

/* 150 ms x id + 0..100 ms. */
static uint64_t stagger(mfc_core *c) { return 150 * (uint64_t)c->cfg.me + rnd(c, 100); }
/* The election timeout after the last leader contact. */
static uint64_t timeout(mfc_core *c) { return c->cfg.dead_ms + stagger(c); }

mfc_core *mfc_new(mfc_config cfg, mfp_persist p, uint64_t now) {
    mfc_core *c = mt_alloc(sizeof *c);
    c->cfg = cfg;
    c->p = p;
    c->role = MFC_FOLLOWER;
    c->rng = cfg.seed | 1;
    if (mfc_fixed(&cfg)) {
        if (c->p.term < 1) c->p.term = 1;
        if (cfg.me == 0) {
            c->role = MFC_LEADER;
            c->has_leader = true;
            c->leader = 0;
        }
    }
    /* At start nobody leads yet: a first pre-vote after a fifth of the dead time (a member that
     * joins a working fleet hears its leader's beat sooner, and pre-votes are refused while a
     * leader is heard, so this never disturbs one). */
    c->election_at = now + c->cfg.dead_ms / 5 + stagger(c);
    return c;
}

void mfc_free(mfc_core *c) {
    if (!c) return;
    mfp_free(&c->p);
    if (c->inflight.on) mfd_doc_free(&c->inflight.doc);
    free(c);
}

const mfc_config *mfc_cfg(const mfc_core *c) { return &c->cfg; }
const mfp_persist *mfc_persist(const mfc_core *c) { return &c->p; }
uint64_t mfc_term(const mfc_core *c) { return c->p.term; }
mfc_role mfc_role_of(const mfc_core *c) { return c->role; }
bool mfc_leader(const mfc_core *c, uint32_t *l) {
    if (c->has_leader && l) *l = c->leader;
    return c->has_leader;
}
const mfd_doc *mfc_committed(const mfc_core *c) { return &c->p.committed; }
const mfd_doc *mfc_accepted(const mfc_core *c) { return &c->p.accepted; }
bool mfc_busy(const mfc_core *c) { return c->inflight.on; }
bool mfc_term_committed(const mfc_core *c) { return c->p.committed.version > 0 && c->p.committed.term == c->p.term; }
uint64_t mfc_next_version(const mfc_core *c) {
    uint64_t a = c->p.accepted.version, b = c->p.committed.version;
    return (a > b ? a : b) + 1;
}
bool mfc_first_doc(const mfc_core *c, mfd_doc *out) {
    if (c->p.accepted.version == 0) return false;
    *out = mfd_doc_clone(&c->p.accepted);
    return true;
}
uint32_t mfc_linked(const mfc_core *c) { return c->linked; }
bool mfc_voted(const mfc_core *c, uint32_t *v) {
    if (c->p.has_voted && v) *v = c->p.voted;
    return c->p.has_voted;
}

uint32_t mfc_alive(const mfc_core *c, uint64_t now) {
    uint32_t m = 0;
    for (uint32_t k = 0; k < c->cfg.n; k++)
        if (k == c->cfg.me || ((c->linked >> k & 1) && (now > c->last_heard[k] ? now - c->last_heard[k] : 0) < c->cfg.dead_ms)) m |= 1u << k;
    return m;
}

bool mfc_has_quorum(const mfc_core *c, uint64_t now) { return (uint32_t)ml_popcount(mfc_alive(c, now)) >= mfc_quorum(&c->cfg); }

static void send(mfc_outs *out, uint32_t k, mfc_msg m) { push(out, (mfc_out){.kind = MFC_OUT_SEND, .to = k, .msg = m}); }

static void send_all(const mfc_core *c, const mfc_msg *m, mfc_outs *out) {
    for (uint32_t k = 0; k < c->cfg.n; k++)
        if (k != c->cfg.me && (c->linked >> k & 1)) send(out, k, mfc_msg_clone(m));
}

static void log_line(mfc_outs *out, char *text) { push(out, (mfc_out){.kind = MFC_OUT_LOG, .text = text}); }
static void persist_out(mfc_outs *out) { push(out, (mfc_out){.kind = MFC_OUT_PERSIST}); }

static void set_leader(mfc_core *c, bool has, uint32_t l, mfc_outs *out) {
    if (c->has_leader == has && (!has || c->leader == l)) return;
    c->has_leader = has;
    c->leader = has ? l : 0;
    push(out, (mfc_out){.kind = MFC_OUT_LEADER, .has_leader = has, .leader = l});
}

static void abandon(mfc_core *c, const char *why, mfc_outs *out) {
    if (!c->inflight.on) return;
    push(out, (mfc_out){.kind = MFC_OUT_DONE, .version = c->inflight.doc.version, .ok = false, .text = mt_strdup(why)});
    mfd_doc_free(&c->inflight.doc);
    c->inflight.on = false;
}

/* A higher term: follow it (no vote yet in it). */
static void adopt(mfc_core *c, uint64_t term, uint64_t now, mfc_outs *out) {
    if (term <= c->p.term) return;
    if (c->role == MFC_LEADER)
        log_line(out, mt_fmt("fleet: term %llu seen; stepping down (was leader of term %llu)", (unsigned long long)term, (unsigned long long)c->p.term));
    c->p.term = term;
    c->p.has_voted = false;
    c->role = MFC_FOLLOWER;
    abandon(c, "the leader changed (a higher term)", out);
    set_leader(c, false, 0, out);
    c->election_at = now + timeout(c);
    persist_out(out);
}

mfc_outs mfc_link(mfc_core *c, uint32_t k, bool up, uint64_t now) {
    mfc_outs out = {0};
    if (k >= c->cfg.n || k == c->cfg.me) return out;
    if (up) {
        c->linked |= 1u << k;
        c->last_heard[k] = now;
        c->caught_up_at[k] = 0;
        /* The pair: system 1's leader is system 0 whenever they are linked. */
        if (mfc_fixed(&c->cfg) && k == 0) {
            c->leader_contact = now;
            set_leader(c, true, 0, &out);
        }
    } else {
        c->linked &= ~(1u << k);
        if (c->has_leader && c->leader == k) {
            /* Its link closed: no leader here now, and no need to wait out dead_ms. */
            set_leader(c, false, 0, &out);
            if (!mfc_fixed(&c->cfg)) {
                uint64_t t = now + stagger(c);
                if (t < c->election_at) c->election_at = t;
            }
        }
    }
    return out;
}

mfc_outs mfc_tick(mfc_core *c, uint64_t now) {
    mfc_outs out = {0};
    if (c->inflight.on && now >= c->inflight.deadline) {
        char *why = mt_fmt("not committed: %d of %u members acknowledged within %llu ms (a majority is %u)", ml_popcount(c->inflight.acks), c->cfg.n,
                           (unsigned long long)c->cfg.dead_ms, mfc_quorum(&c->cfg));
        abandon(c, why, &out);
        free(why);
    }
    if (mfc_fixed(&c->cfg)) return out;
    if (c->role == MFC_LEADER) {
        if (!mfc_has_quorum(c, now)) {
            log_line(&out, mt_fmt("fleet: leader of term %llu hears only %d of %u members (a majority is %u): stepping down; no changes until a majority is back",
                                  (unsigned long long)c->p.term, ml_popcount(mfc_alive(c, now)), c->cfg.n, mfc_quorum(&c->cfg)));
            c->role = MFC_FOLLOWER;
            abandon(c, "the leader lost its majority", &out);
            set_leader(c, false, 0, &out);
            c->election_at = now + timeout(c);
        }
    } else if (now >= c->election_at) {
        /* Pre-vote first: no term change unless a majority would elect us. */
        c->role = MFC_PRE_CANDIDATE;
        c->votes = 1u << c->cfg.me;
        if (c->has_leader) set_leader(c, false, 0, &out);
        c->election_at = now + c->cfg.dead_ms / 2 + stagger(c);
        mfc_msg m = {.kind = MFC_VOTE_REQ, .pre = true, .term = c->p.term + 1, .acc = mfc_doc_id(&c->p.accepted)};
        send_all(c, &m, &out);
    }
    return out;
}

static bool up_to_date(const mfc_core *c, mfc_id acc) { return mfc_id_cmp(acc, mfc_doc_id(&c->p.accepted)) >= 0; }

static bool hears_leader(const mfc_core *c, uint64_t now) {
    if (!c->has_leader) return false;
    if (c->leader == c->cfg.me) return true;
    return (c->linked >> c->leader & 1) && (now > c->leader_contact ? now - c->leader_contact : 0) < c->cfg.dead_ms;
}

/* The leader's commit point reached us: commit the accepted document if it is that one. Only
 * forward: a commit point at or below ours is old news. */
static void follow_commit(mfc_core *c, mfc_id commit, mfc_outs *out) {
    if (mfc_id_cmp(commit, mfc_doc_id(&c->p.committed)) > 0 && mfc_id_cmp(commit, mfc_doc_id(&c->p.accepted)) == 0 && commit.version > 0) {
        mfd_doc_free(&c->p.committed);
        c->p.committed = mfd_doc_clone(&c->p.accepted);
        persist_out(out);
        push(out, (mfc_out){.kind = MFC_OUT_COMMITTED});
    }
}

/* A follower whose beat shows it behind gets the leader's accepted document (at most once a
 * second), or -- after an abandoned proposal of this term left accepted ahead of committed -- the
 * committed one (fleet.rs catch_up says why). */
static void catch_up(mfc_core *c, uint32_t k, mfc_id commit, mfc_id acc, uint64_t now, mfc_outs *out) {
    bool stuck = !c->inflight.on && mfc_term_committed(c) && mfc_id_cmp(mfc_doc_id(&c->p.accepted), mfc_doc_id(&c->p.committed)) != 0 &&
                 mfc_id_cmp(commit, mfc_doc_id(&c->p.committed)) < 0;
    const mfd_doc *want = stuck ? &c->p.committed : &c->p.accepted;
    if (mfc_id_cmp(acc, mfc_doc_id(want)) == 0 || want->version == 0) return;
    uint64_t last = c->caught_up_at[k];
    if (last != 0 && (now > last ? now - last : 0) < 1000) return;
    c->caught_up_at[k] = now;
    send(out, k, (mfc_msg){.kind = MFC_STATE, .term = c->p.term, .doc = mfd_doc_clone(want)});
}

static void commit_inflight(mfc_core *c, mfc_outs *out) {
    mfc_id ver = mfc_doc_id(&c->inflight.doc);
    mfd_doc_free(&c->p.committed);
    c->p.committed = c->inflight.doc;
    c->inflight.on = false;
    c->inflight.doc = (mfd_doc){0};
    persist_out(out);
    push(out, (mfc_out){.kind = MFC_OUT_COMMITTED});
    mfc_msg m = {.kind = MFC_COMMIT, .term = c->p.term, .ver = ver};
    send_all(c, &m, out);
    push(out, (mfc_out){.kind = MFC_OUT_DONE, .version = ver.version, .ok = true});
}

mfc_msg mfc_beat(const mfc_core *c) {
    return (mfc_msg){.kind = MFC_BEAT, .term = c->p.term, .leader = c->role == MFC_LEADER, .commit = mfc_doc_id(&c->p.committed), .acc = mfc_doc_id(&c->p.accepted)};
}

mfc_outs mfc_recv(mfc_core *c, uint32_t from, mfc_msg m, uint64_t now) {
    mfc_outs out = {0};
    if (from >= c->cfg.n || from == c->cfg.me) {
        mfc_msg_free(&m);
        return out;
    }
    c->last_heard[from] = now;
    bool fixed = mfc_fixed(&c->cfg);
    switch (m.kind) {
    case MFC_BEAT:
        if (fixed) {
            if (from == 0 && m.leader) {
                c->leader_contact = now;
                set_leader(c, true, 0, &out);
                follow_commit(c, m.commit, &out);
            }
            if (c->role == MFC_LEADER) catch_up(c, from, m.commit, m.acc, now, &out);
            break;
        }
        adopt(c, m.term, now, &out);
        if (m.term < c->p.term) break;
        if (m.leader) {
            if (c->role == MFC_LEADER) break; /* same term, two leaders: impossible (one vote each) */
            c->role = MFC_FOLLOWER;
            c->leader_contact = now;
            c->election_at = now + timeout(c);
            set_leader(c, true, from, &out);
            follow_commit(c, m.commit, &out);
        } else if (c->role == MFC_LEADER) {
            catch_up(c, from, m.commit, m.acc, now, &out);
        }
        break;
    case MFC_VOTE_REQ:
        if (fixed) break;
        if (m.pre) {
            bool granted = m.term > c->p.term && up_to_date(c, m.acc) && !hears_leader(c, now);
            send(&out, from, (mfc_msg){.kind = MFC_VOTE, .pre = true, .term = m.term, .granted = granted});
        } else {
            /* A real election: only after a pre-vote majority, so a higher term here is legitimate. */
            adopt(c, m.term, now, &out);
            bool granted = m.term == c->p.term && c->role != MFC_LEADER && (!c->p.has_voted || c->p.voted == from) && up_to_date(c, m.acc);
            if (granted) {
                if (!c->p.has_voted) {
                    c->p.has_voted = true;
                    c->p.voted = from;
                    persist_out(&out);
                }
                c->election_at = now + timeout(c);
            }
            send(&out, from, (mfc_msg){.kind = MFC_VOTE, .pre = false, .term = c->p.term, .granted = granted});
        }
        break;
    case MFC_VOTE:
        if (m.pre) {
            if (c->role == MFC_PRE_CANDIDATE && m.term == c->p.term + 1 && m.granted) {
                c->votes |= 1u << from;
                if ((uint32_t)ml_popcount(c->votes) >= mfc_quorum(&c->cfg)) {
                    /* A real election. */
                    c->role = MFC_CANDIDATE;
                    c->p.term += 1;
                    c->p.has_voted = true;
                    c->p.voted = c->cfg.me;
                    c->votes = 1u << c->cfg.me;
                    c->election_at = now + c->cfg.dead_ms / 2 + stagger(c);
                    persist_out(&out);
                    log_line(&out, mt_fmt("fleet: election for term %llu (pre-votes from 0x%x)", (unsigned long long)c->p.term, c->votes));
                    mfc_msg r = {.kind = MFC_VOTE_REQ, .pre = false, .term = c->p.term, .acc = mfc_doc_id(&c->p.accepted)};
                    send_all(c, &r, &out);
                }
            }
        } else {
            adopt(c, m.term, now, &out);
            if (c->role == MFC_CANDIDATE && m.term == c->p.term && m.granted) {
                c->votes |= 1u << from;
                if ((uint32_t)ml_popcount(c->votes) >= mfc_quorum(&c->cfg)) {
                    c->role = MFC_LEADER;
                    log_line(&out, mt_fmt("fleet: system %u leads term %llu (votes 0x%x)", c->cfg.me, (unsigned long long)c->p.term, c->votes));
                    set_leader(c, true, c->cfg.me, &out);
                    mfc_msg b = mfc_beat(c);
                    send_all(c, &b, &out);
                }
            }
        }
        break;
    case MFC_STATE: {
        if (!fixed) adopt(c, m.term, now, &out);
        if (m.term < c->p.term || c->role == MFC_LEADER || (fixed && from != 0)) break;
        /* The current leader's document is accepted as is (it never sends one below its committed). */
        c->role = MFC_FOLLOWER;
        c->leader_contact = now;
        c->election_at = now + timeout(c);
        set_leader(c, true, from, &out);
        mfc_id ver = mfc_doc_id(&m.doc);
        mfd_doc_free(&c->p.accepted);
        c->p.accepted = m.doc;
        m.doc = (mfd_doc){0};
        persist_out(&out);
        send(&out, from, (mfc_msg){.kind = MFC_STATE_ACK, .term = c->p.term, .ver = ver});
        break;
    }
    case MFC_STATE_ACK:
        if (m.term != c->p.term || c->role != MFC_LEADER) break;
        if (c->inflight.on && mfc_id_cmp(mfc_doc_id(&c->inflight.doc), m.ver) == 0) {
            c->inflight.acks |= 1u << from;
            if ((uint32_t)ml_popcount(c->inflight.acks) >= mfc_quorum(&c->cfg)) commit_inflight(c, &out);
            break;
        }
        /* A catch-up of the committed document landed. */
        if (mfc_id_cmp(m.ver, mfc_doc_id(&c->p.committed)) == 0) send(&out, from, (mfc_msg){.kind = MFC_COMMIT, .term = c->p.term, .ver = m.ver});
        break;
    case MFC_COMMIT:
        if (m.term == c->p.term && c->has_leader && c->leader == from) follow_commit(c, m.ver, &out);
        break;
    }
    mfc_msg_free(&m);
    return out;
}

bool mfc_propose(mfc_core *c, mfd_doc doc, uint64_t now, uint64_t *version, mfc_outs *outs, char **err) {
    if (c->role != MFC_LEADER) {
        *err = c->has_leader ? mt_fmt("not the leader (system %u is)", c->leader) : mt_strdup("no leader");
        mfd_doc_free(&doc);
        return false;
    }
    if (c->inflight.on) {
        *err = mt_strdup("busy: another change is being committed");
        mfd_doc_free(&doc);
        return false;
    }
    uint32_t alive = mfc_alive(c, now);
    if ((uint32_t)ml_popcount(alive) < mfc_quorum(&c->cfg)) {
        ml_list l = ml_members(alive);
        char *t = ml_text(&l);
        *err = mt_fmt("no quorum: %d of %u members reachable (%s), a majority is %u", ml_popcount(alive), c->cfg.n, t, mfc_quorum(&c->cfg));
        free(t);
        ml_free(&l);
        mfd_doc_free(&doc);
        return false;
    }
    doc.term = c->p.term;
    doc.version = mfc_next_version(c);
    *version = doc.version;
    mfd_doc_free(&c->p.accepted);
    c->p.accepted = mfd_doc_clone(&doc);
    mfc_outs out = {0};
    persist_out(&out);
    c->inflight = (inflight){true, mfd_doc_clone(&doc), 1u << c->cfg.me, now + c->cfg.dead_ms};
    mfc_msg m = {.kind = MFC_STATE, .term = c->p.term, .doc = doc};
    send_all(c, &m, &out);
    mfd_doc_free(&m.doc);
    /* The pair's coordinator commits alone (its member follows when it hears it). */
    if (mfc_quorum(&c->cfg) <= 1) commit_inflight(c, &out);
    *outs = out;
    return true;
}
