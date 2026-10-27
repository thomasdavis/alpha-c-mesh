/* load.rs's GPU share arithmetic, in the Rust's float widths and order. */
#include "mesh/load_share.h"


mesh_nl mesh_nl_delta(const mesh_nl *now, const mesh_nl *prev) {
    mesh_nl d = {0, 0, 0, 0};
    if (now->obs < prev->obs || now->lost < prev->lost) return d;
    d.obs = now->obs - prev->obs;
    d.lost = now->lost - prev->lost;
    d.gaps = now->gaps - prev->gaps;
    d.queue_us = now->queue_us - prev->queue_us;
    return d;
}

void mesh_nl_add(mesh_nl *s, const mesh_nl *d) {
    s->obs += d->obs;
    s->lost += d->lost < d->obs ? d->lost : d->obs;
    s->gaps += d->gaps;
    s->queue_us += d->queue_us;
}

mesh_share mesh_share_of(const mesh_nl *d) {
    mesh_share s = {false, 0, 0, 0, 0};
    if (d->obs == 0) return s;
    uint64_t lost = d->lost < d->obs ? d->lost : d->obs;
    double gaps = (double)d->gaps;
    s.has_share = true;
    s.share = (float)(1.0 - (double)lost / (double)d->obs);
    if (d->gaps > 0) {
        s.slice_us = (float)((double)(d->obs - lost) / gaps / 1e3);
        s.gap_us = (float)((double)lost / gaps / 1e3);
        s.queue_us = (float)((double)d->queue_us / gaps);
    }
    return s;
}

uint32_t mesh_quantile(const uint32_t *sorted, size_t n, double q) {
    if (n == 0) return 0;
    /* f64::round (half away from zero) of a non-negative x, without libm. */
    double x = (double)(n - 1) * q;
    size_t r = (size_t)x;
    if (x - (double)r >= 0.5) r++;
    return sorted[r];
}
