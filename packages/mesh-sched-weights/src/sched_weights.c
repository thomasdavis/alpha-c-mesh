/* Weights::default and Weights::from_keys. */
#include "mesh/sched_weights.h"
#include "mesh/parse.h"
#include "mesh/rust_fmt.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <string.h>

msw_weights msw_default(void) {
    return (msw_weights){.perf = 1.0, .load = 0.6, .mem = 0.3, .spread = 0.2, .rtt = 0.3, .prefer = 0.25, .sticky = 0.1, .owner = 0.6,
                         .improve = 0.25, .hold_ms = 10000, .dwell_ms = 60000, .per_min = 3, .replace_ms = 3000, .owner_share = 0.3f,
                         .owner_psi = 0.25f, .owner_ms = 6000, .tick_ms = 1000, .reserve = 0.25f};
}

static bool num(const ms_map *m, const char *k, double d, double *out, char **err) {
    const char *v = ms_get(m, k);
    if (!v) {
        *out = d;
        return true;
    }
    if (mp_f64(v, out)) return true;
    char *q = rf_debug_str(v);
    *err = mt_fmt("cluster: %s = %s: a number", k, q);
    free(q);
    return false;
}

static bool integer(const ms_map *m, const char *k, uint64_t d, uint64_t *out, char **err) {
    const char *v = ms_get(m, k);
    if (!v) {
        *out = d;
        return true;
    }
    if (mp_u64(v, out)) return true;
    char *q = rf_debug_str(v);
    *err = mt_fmt("cluster: %s = %s: an integer", k, q);
    free(q);
    return false;
}

bool msw_from_keys(const ms_map *m, msw_weights *out, char **err) {
    msw_weights w = msw_default();
    double f;
    uint64_t u;
    if (!num(m, "sched_improve", w.improve, &w.improve, err)) return false;
    if (!integer(m, "sched_hold_ms", w.hold_ms, &w.hold_ms, err)) return false;
    if (!integer(m, "sched_dwell_ms", w.dwell_ms, &w.dwell_ms, err)) return false;
    if (!integer(m, "sched_per_min", w.per_min, &u, err)) return false;
    w.per_min = (uint32_t)u;
    if (!integer(m, "sched_replace_ms", w.replace_ms, &w.replace_ms, err)) return false;
    if (!num(m, "sched_owner_share", (double)w.owner_share, &f, err)) return false;
    w.owner_share = (float)f;
    if (!num(m, "sched_owner_psi", (double)w.owner_psi, &f, err)) return false;
    w.owner_psi = (float)f;
    if (!integer(m, "sched_owner_ms", w.owner_ms, &w.owner_ms, err)) return false;
    if (!integer(m, "sched_tick_ms", w.tick_ms, &u, err)) return false;
    w.tick_ms = u > 100 ? u : 100;
    if (!num(m, "sched_reserve", (double)w.reserve, &f, err)) return false;
    w.reserve = (float)f;
    if (!(w.improve >= 0.0 && w.improve <= 10.0) || !(w.reserve >= 0.0f && w.reserve < 1.0f)) {
        *err = mt_strdup("cluster: sched_improve 0..10, sched_reserve 0..1");
        return false;
    }
    *out = w;
    return true;
}

bool msw_eq(const msw_weights *a, const msw_weights *b) {
    return a->perf == b->perf && a->load == b->load && a->mem == b->mem && a->spread == b->spread && a->rtt == b->rtt && a->prefer == b->prefer &&
           a->sticky == b->sticky && a->owner == b->owner && a->improve == b->improve && a->hold_ms == b->hold_ms && a->dwell_ms == b->dwell_ms &&
           a->per_min == b->per_min && a->replace_ms == b->replace_ms && a->owner_share == b->owner_share && a->owner_psi == b->owner_psi &&
           a->owner_ms == b->owner_ms && a->tick_ms == b->tick_ms && a->reserve == b->reserve;
}
