/* The perf board (shared.rs PerfBoard): every member's last F_PERF, and the link RTTs measured from the
 * echoes of ours. */
#include "relay.h"

#include <stdlib.h>
#include <string.h>

void board_init(board_t *b) { pthread_mutex_init(&b->mu, NULL); }

void board_free(board_t *b) {
    for (uint32_t k = 0; k < MAXS; k++)
        if (b->has[k]) msr_free(&b->rep[k]);
    pthread_mutex_destroy(&b->mu);
}

/* A deep copy (the text form is lossy: it rounds the floats). */
static msr_report report_clone(const msr_report *r) {
    msr_report c = {.t_us = r->t_us, .cpus = r->cpus, .big = r->big, .little = r->little, .ram_mb = r->ram_mb, .disk_free_mb = r->disk_free_mb,
                    .heap_kb = r->heap_kb, .slots_free = r->slots_free};
    for (size_t i = 0; i < r->nprogs; i++) msr_add_prog(&c, r->progs[i].name, &r->progs[i].perf);
    for (size_t i = 0; i < r->nfails; i++) msr_add_fail(&c, r->fails[i].name, r->fails[i].hash, r->fails[i].unsupported, r->fails[i].why);
    for (size_t i = 0; i < r->nrtt; i++) msr_add_rtt(&c, r->rtt[i].k, r->rtt[i].us);
    for (size_t i = 0; i < r->necho; i++) msr_add_echo(&c, r->echo[i].k, r->echo[i].t, r->echo[i].held);
    return c;
}

void board_report(mpr_relay *r, uint32_t from, const char *text) {
    msr_report rep;
    char *err = NULL;
    if (!msr_decode(text, &rep, &err)) {
        relay_log(r, true, mt_fmt("peer: system %u: %s", from, err));
        free(err);
        return;
    }
    if (from >= MAXS) {
        msr_free(&rep);
        return;
    }
    uint64_t now = mpr_mono_us();
    board_t *b = &r->board;
    pthread_mutex_lock(&b->mu);
    /* Its echo of our last send: the link's round trip less the time it held it. */
    for (size_t i = 0; i < rep.necho; i++)
        if (rep.echo[i].k == r->me) {
            uint64_t t = rep.echo[i].t, held = rep.echo[i].held;
            uint64_t rtt = now > t ? now - t : 0;
            rtt = rtt > held ? rtt - held : 0;
            if (t > 0 && rtt < 10000000) b->rtt_us[from] = mof32_some((float)rtt);
            break;
        }
    if (b->has[from]) msr_free(&b->rep[from]);
    b->rep[from] = rep;
    b->at_us[from] = now;
    b->has[from] = true;
    pthread_mutex_unlock(&b->mu);
}

void mpr_board_own(mpr_relay *r, const msr_report *rep) {
    board_t *b = &r->board;
    pthread_mutex_lock(&b->mu);
    if (b->has[r->me]) msr_free(&b->rep[r->me]);
    b->rep[r->me] = report_clone(rep);
    b->at_us[r->me] = mpr_mono_us();
    b->has[r->me] = true;
    pthread_mutex_unlock(&b->mu);
}

size_t mpr_board_echoes(mpr_relay *r, uint32_t *k, uint64_t *t, uint64_t *held) {
    board_t *b = &r->board;
    size_t n = 0;
    uint64_t now = mpr_mono_us();
    pthread_mutex_lock(&b->mu);
    for (uint32_t j = 0; j < r->n; j++)
        if (j != r->me && b->has[j]) k[n] = j, t[n] = b->rep[j].t_us, held[n++] = now - b->at_us[j];
    pthread_mutex_unlock(&b->mu);
    return n;
}

size_t mpr_board_rtts(mpr_relay *r, uint32_t *k, float *us) {
    board_t *b = &r->board;
    size_t n = 0;
    pthread_mutex_lock(&b->mu);
    for (uint32_t j = 0; j < r->n; j++)
        if (b->rtt_us[j].some) k[n] = j, us[n++] = b->rtt_us[j].v;
    pthread_mutex_unlock(&b->mu);
    return n;
}
