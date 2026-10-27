/* @ajax/mesh-sched-score: Nomad-style feasibility, then score (sched.rs). `msc_filter` gives the
 * executor a program would run on at a member, or the first reason it cannot run there;
 * `msc_score_terms` the weighted terms of running it there: the processor share it would get times
 * its measured speed, CPU headroom after the move, memory headroom, spread or pack, link RTT
 * (groups), prefer, stickiness for the current member, and the owner penalty.
 *
 * Other schedulers (the jobs' leases) use `msc_eligible`, `msc_filter`, `msc_score` and
 * `msc_explain` directly. */
#ifndef MESH_SCHED_SCORE_H
#define MESH_SCHED_SCORE_H

#include "mesh/sched_member.h"
#include "mesh/sched_needs.h"
#include "mesh/sched_weights.h"

#include <stdbool.h>
#include <stdint.h>

/* The executor n would run on at m (written to exec, 16 bytes), or false with *err the reason. */
bool msc_filter(const msm_member *m, const msn_needs *n, char exec[16], char **err);
/* Whether n may run on m at all (the reservations and colocation aside). */
bool msc_eligible(const msm_member *m, const msn_needs *n);

/* A score's terms (each already weighted) and their sum. */
typedef struct {
    char exec[16];
    double total, perf, load, mem, spread, rtt, prefer, sticky, owner;
    bool explore;  /* the speed is a prior (nothing measured on this member yet) */
    mof32 share;   /* the raw figures behind the terms */
    mof32 task_us;
    float cpu_after;
} msc_score_t;

/* "0.812 = perf 0.50 load 0.54 ... (cuda, gpu share 0.50, cpu after 0.10)": a new string. */
char *msc_explain(const msc_score_t *s);

/* What a score depends on beyond the member and the needs. */
typedef struct {
    const msw_weights *w;
    bool current;              /* it runs on this member now (no load to add; the sticky bonus) */
    const msm_perf *here;      /* its measured performance here (NULL: none) ... */
    mof32 best_task_us;        /* ... and the best measured time per task anywhere */
    float own_cpu;             /* the CPU cores it uses (measured where it runs, else its reservation) */
    uint32_t placed, max_placed; /* programs already on this member, and the most on any (spread / pack) */
    mof32 rtt_ms;              /* the mean RTT to its partners in ms (a group); none: not scored */
} msc_cx;

msc_cx msc_cx_simple(const msw_weights *w, const msm_perf *here);
/* The score terms of running n on m (it passed msc_filter). */
msc_score_t msc_score_terms(const msm_member *m, const msn_needs *n, const msc_cx *cx);
/* The plain score of n on m (no placement context). */
double msc_score(const msm_member *m, const msn_needs *n, const msm_perf *here, const msw_weights *w);

#endif
