/* Ticket rings and the publishing policies. */
#include "mesh/publish.h"
#include "mesh/buf.h"

#include <stdlib.h>
#include <string.h>

void mesh_pring_init(mesh_pring *r, uint32_t slot) {
    memset(r, 0, sizeof *r);
    r->ctl = mesh_prog_ctl(slot);
}

void mesh_pring_free(mesh_pring *r) {
    for (size_t i = 0; i < r->n; i++) mesh_reply_drop(r->inflight[i].kind.reply);
    free(r->inflight);
    r->inflight = NULL;
    r->n = r->cap = 0;
}

static size_t entry(const mesh_pring *r, uint32_t t) { return r->ctl + MESH_P_RING + MESH_ENTRY_BYTES * (size_t)(t % MESH_PROG_RING); }

uint32_t mesh_pring_room(const mesh_pring *r) {
    if (!r->n) return MESH_PROG_RING;
    uint32_t used = r->tail - r->inflight[0].ticket;
    return used >= MESH_PROG_RING ? 0 : MESH_PROG_RING - used;
}

bool mesh_pring_can_publish(const mesh_pring *r) { return mesh_pring_room(r) > 0; }

bool mesh_pring_publish(mesh_pring *r, const mesh_mailbox *mb, uint32_t op, uint32_t arg, mesh_kind kind, uint32_t *ticket) {
    if (!mesh_pring_can_publish(r)) return false;
    uint32_t t = r->tail;
    size_t e = entry(r, t);
    mesh_mailbox_set(mb, e + MESH_E_OP, op);
    mesh_mailbox_set(mb, e + MESH_E_ARG, arg);
    mesh_mailbox_set(mb, e + MESH_E_SLOT, t % MESH_PROG_RESULTS);
    mesh_mailbox_publish(mb, e + MESH_E_SEQ, t + 1);
    r->tail = t + 1;
    mesh_mailbox_set64(mb, r->ctl + MESH_P_RING_TAIL, r->tail);
    if (r->n == r->cap) {
        r->cap = r->cap ? 2 * r->cap : 16;
        r->inflight = mesh_xrealloc(r->inflight, r->cap * sizeof *r->inflight);
    }
    r->inflight[r->n].ticket = t;
    r->inflight[r->n].kind = kind;
    r->n++;
    if (ticket) *ticket = t;
    return true;
}

void mesh_pring_collect(mesh_pring *r, const mesh_mailbox *mb, mesh_done **done, size_t *n) {
    *done = NULL;
    *n = 0;
    if (!r->n) return;
    size_t keep = 0, k = 0;
    mesh_done *d = NULL;
    for (size_t i = 0; i < r->n; i++) {
        mesh_out o = r->inflight[i];
        size_t c = o.ticket % MESH_PROG_RESULTS;
        if (mesh_mailbox_get(mb, r->ctl + MESH_P_MARKS + 4 * c) == o.ticket + 1) {
            if (!d) d = mesh_xmalloc(r->n * sizeof *d);
            d[k].out = o;
            d[k].result = mesh_mailbox_get(mb, r->ctl + MESH_P_RESULTS + 4 * c);
            k++;
        } else r->inflight[keep++] = o;
    }
    r->n = keep;
    *done = d;
    *n = k;
}

bool mesh_pring_idle(const mesh_pring *r) { return r->n == 0; }

static uint32_t count(const mesh_pring *r, mesh_tk tk) {
    uint32_t c = 0;
    for (size_t i = 0; i < r->n; i++) c += r->inflight[i].kind.tk == tk;
    return c;
}

uint32_t mesh_pring_auto_in_flight(const mesh_pring *r) { return count(r, MESH_TK_AUTO); }
uint32_t mesh_pring_follow_in_flight(const mesh_pring *r) { return count(r, MESH_TK_FOLLOW); }
size_t mesh_pring_in_flight(const mesh_pring *r) { return r->n; }
bool mesh_pring_has_req(const mesh_pring *r) { return count(r, MESH_TK_REQ) > 0; }
bool mesh_pring_has_init(const mesh_pring *r) { return count(r, MESH_TK_INIT) > 0; }

void mesh_pring_rewind(mesh_pring *r, const mesh_mailbox *mb, mesh_out **outs, size_t *n) {
    mesh_mailbox_set(mb, r->ctl + MESH_P_RING_HEAD, r->tail);
    mesh_mailbox_set(mb, r->ctl + MESH_P_ONLINE, 0);
    mesh_mailbox_set(mb, r->ctl + MESH_P_HALTED, 0);
    *outs = r->inflight;
    *n = r->n;
    r->inflight = NULL;
    r->n = r->cap = 0;
}

uint32_t mesh_due(const mesh_manifest *m, uint32_t n, const mesh_pring *r, int64_t *last, int64_t now) {
    uint32_t a = mesh_pring_auto_in_flight(r);
    if (m->batch && a > 0) return 0;
    switch (m->policy) {
    case MESH_POLICY_CONTINUOUS: return n - (a < n ? a : n);
    case MESH_POLICY_PERIODIC: {
        int64_t period = (int64_t)(1000000000ull / m->hz);
        int64_t since = now > *last ? now - *last : 0;
        if (since < period) return 0;
        /* Keep the cadence; after a stall (or a stop) restart it from now. */
        *last = since < 2 * period ? *last + period : now;
        return n;
    }
    default: return 0;
    }
}

uint32_t mesh_publish_auto(const mesh_mailbox *mb, const mesh_manifest *m, uint32_t view_off, mesh_pring *r, int64_t *last, int64_t now) {
    uint32_t k = m->fanout;
    if (m->batch && mesh_pring_room(r) < k) return 0;
    uint32_t n = mesh_due(m, k, r, last, now);
    uint32_t base = m->arg_mode == MESH_ARG_VIEW_U32 ? mesh_mailbox_get(mb, (size_t)view_off + m->arg_off) : 0;
    uint32_t sent = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!mesh_pring_publish(r, mb, MESH_OP_TASK, base + i, mesh_kind_of(MESH_TK_AUTO), NULL)) break;
        sent++;
    }
    return sent;
}

uint32_t mesh_publish_parts(const mesh_mailbox *mb, const mesh_manifest *m, uint32_t view_off, mesh_pring *r, int64_t *last,
                            int64_t now, uint32_t parts) {
    uint32_t k = 0;
    for (int b = 0; b < 32; b++) k += (parts >> b) & 1u;
    if (k == 0 || mesh_pring_room(r) < k) return 0;
    if (mesh_due(m, k, r, last, now) < k) return 0;
    uint32_t base = m->arg_mode == MESH_ARG_VIEW_U32 ? mesh_mailbox_get(mb, (size_t)view_off + m->arg_off) : 0;
    uint32_t sent = 0;
    for (uint32_t b = 0; b < 32; b++) {
        if (!((parts >> b) & 1u)) continue;
        if (!mesh_pring_publish(r, mb, MESH_OP_TASK, base + b, mesh_kind_of(MESH_TK_AUTO), NULL)) break;
        sent++;
    }
    return sent;
}
