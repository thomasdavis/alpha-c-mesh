/* peer_sched.rs: ViewWait and view_take; LoadAvg. */
#include "mesh/peer_view.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <string.h>

mpv_wait mpv_wait_new(uint32_t from, uint64_t deadline, size_t limit) { return (mpv_wait){.from = from, .deadline = deadline, .limit = limit}; }

void mpv_wait_free(mpv_wait *w) {
    free(w->buf);
    rs_free(&w->have);
    *w = (mpv_wait){0};
}

int mpv_take(mpv_wait *w, uint32_t total32, uint32_t off32, const uint8_t *b, size_t n, char **err) {
    size_t total = total32, off = off32;
    if (total > w->limit) {
        *err = mt_fmt("F_VIEW of %zu bytes: the view is at most %zu bytes", total, w->limit);
        return -1;
    }
    if (!w->has_total) {
        w->has_total = true;
        w->total = total;
        w->buf = mt_alloc(total ? total : 1);
    } else if (w->total != total) {
        *err = mt_fmt("F_VIEW chunks disagree on the view's size (%zu, then %zu)", w->total, total);
        return -1;
    }
    if (off + n < off || off + n > total) {
        *err = mt_fmt("F_VIEW chunk %zu+%zu past %zu", off, n, total);
        return -1;
    }
    if (n) memcpy(w->buf + off, b, n);
    rs_insert(&w->have, off, off + n);
    return rs_len(&w->have) == total ? 1 : 0;
}

void mpv_load_add(mpv_load *l, uint64_t at, float cpu, mof32 psi_cpu, mof32 psi_mem, mof32 share) {
    if (at == l->at) return;
    l->at = at;
#define EASE(a, x) ((a) + MPV_LOAD_ALPHA * ((x) - (a)))
#define OPT(a, x) ((a).some && (x).some ? mof32_some(EASE((a).v, (x).v)) : (x))
    l->cpu = EASE(l->cpu, cpu);
    l->psi_cpu = OPT(l->psi_cpu, psi_cpu);
    l->psi_mem = OPT(l->psi_mem, psi_mem);
    l->share = OPT(l->share, share);
}
