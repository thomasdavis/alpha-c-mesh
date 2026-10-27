/* place.rs: read_state, write_state, gather, scatter, sys_nibbles, Composer. */
#include "mesh/place_compose.h"
#include "mesh/rust_fmt.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <string.h>

mpw_bytes mpc_read_state(const mps_spec *s, mpc_read_fn read, void *ctx) {
    mpw_bytes o = {0};
    for (size_t i = 0; i < s->nstate; i++) {
        uint8_t *b = mt_alloc(s->state[i].len ? s->state[i].len : 1);
        read(ctx, s->state[i].off, s->state[i].len, b);
        mpw_put(&o, b, s->state[i].len);
        free(b);
    }
    return o;
}

bool mpc_write_state(const mps_spec *s, const uint8_t *b, size_t n, mpc_write_fn write, void *ctx, char **err) {
    if (n != mps_state_bytes(s)) {
        *err = mt_fmt("state of %zu bytes, the manifest declares %zu", n, mps_state_bytes(s));
        return false;
    }
    size_t at = 0;
    for (size_t i = 0; i < s->nstate; i++) {
        write(ctx, s->state[i].off, b + at, s->state[i].len);
        at += s->state[i].len;
    }
    return true;
}

mpw_bytes mpc_gather(const mps_spec *s, uint32_t mask, mpc_read_fn read, void *ctx) {
    mpw_bytes o = {0};
    mps_region *r = mt_alloc((s->nout ? s->nout : 1) * sizeof *r);
    for (uint32_t b = 0; b < s->fanout; b++) {
        if (!(mask >> b & 1)) continue;
        mps_part(s, b, r);
        for (size_t i = 0; i < s->nout; i++) {
            uint8_t *x = mt_alloc(r[i].len ? r[i].len : 1);
            read(ctx, r[i].off, r[i].len, x);
            mpw_put(&o, x, r[i].len);
            free(x);
        }
    }
    free(r);
    return o;
}

static int popcount(uint32_t x) {
    int n = 0;
    for (; x; x &= x - 1) n++;
    return n;
}

bool mpc_scatter(const mps_spec *s, uint8_t *buf, size_t nbuf, uint32_t mask, const uint8_t *data, size_t ndata, char **err) {
    size_t want = (size_t)popcount(mask) * mps_part_bytes(s);
    if (ndata != want || nbuf != mps_out_bytes(s)) {
        char h[24];
        *err = mt_fmt("frame data of %zu bytes, parts %s need %zu", ndata, rf_hex_alt(h, mask), want);
        return false;
    }
    mps_region *r = mt_alloc((s->nout ? s->nout : 1) * sizeof *r);
    size_t at = 0;
    for (uint32_t b = 0; b < s->fanout; b++) {
        if (!(mask >> b & 1)) continue;
        mps_part_in_buffer(s, b, r);
        for (size_t i = 0; i < s->nout; i++) {
            memcpy(buf + r[i].off, data + at, r[i].len);
            at += r[i].len;
        }
    }
    free(r);
    return true;
}

uint32_t mpc_sys_nibbles(uint32_t mask, uint32_t sys) {
    uint32_t w = 0;
    for (uint32_t b = 0; b < 8; b++)
        if (mask >> b & 1) w |= (sys & 15) << (4 * b);
    return w;
}

static void pending_free(mpc_pending *p) {
    free(p->buf);
    p->buf = NULL;
}

static void remove_at(mpc_composer *c, size_t i, bool free_buf) {
    if (free_buf) pending_free(&c->q[i]);
    memmove(c->q + i, c->q + i + 1, (c->n - i - 1) * sizeof *c->q);
    c->n--;
}

int mpc_add(mpc_composer *c, const mps_spec *s, uint32_t frame, uint32_t parts, uint32_t sys, const uint8_t *data, size_t n, uint64_t now, mpc_whole *whole,
            char **err) {
    size_t i = 0;
    for (; i < c->n; i++)
        if (c->q[i].frame == frame) break;
    if (i == c->n) {
        if (c->n >= MPC_PENDING) remove_at(c, 0, true);
        c->q[c->n++] = (mpc_pending){frame, 0, 0, mt_alloc(mps_out_bytes(s) ? mps_out_bytes(s) : 1), now};
        i = c->n - 1;
    }
    mpc_pending *p = &c->q[i];
    if (p->have & parts) {
        char h[24];
        *err = mt_fmt("frame %u: parts %s arrived twice", frame, rf_hex_alt(h, parts));
        return -1;
    }
    if (!mpc_scatter(s, p->buf, mps_out_bytes(s), parts, data, n, err)) return -1;
    p->have |= parts;
    p->sys |= mpc_sys_nibbles(parts, sys);
    uint32_t all = s->fanout >= 32 ? UINT32_MAX : (1u << s->fanout) - 1;
    if (p->have != all) return 0;
    *whole = (mpc_whole){p->frame, p->sys, p->buf, mps_out_bytes(s), p->t0};
    remove_at(c, i, false);
    /* Older frames can no longer be shown (a newer one is): drop them. */
    for (size_t j = c->n; j-- > 0;)
        if (!((int32_t)(c->q[j].frame - frame) > 0)) remove_at(c, j, true);
    return 1;
}

void mpc_clear(mpc_composer *c) {
    for (size_t i = 0; i < c->n; i++) pending_free(&c->q[i]);
    c->n = 0;
}
