/* @ajax/mesh-sched-report: a member's F_PERF (text): its capacity, its programs' measured
 * performance, its reconcile failures, the link RTTs it measured and the echoes that let the others
 * measure theirs. */
#ifndef MESH_SCHED_REPORT_H
#define MESH_SCHED_REPORT_H

#include "mesh/sched_member.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char *name;
    msm_perf perf;
} msr_prog;

/* A program this member's reconciler failed. */
typedef struct {
    char *name;
    uint64_t hash;
    bool unsupported;
    char *why;
} msr_fail;

typedef struct {
    uint32_t k;
    float us;
} msr_rtt;

/* (member, its t_us we received last, µs we held it before this send) */
typedef struct {
    uint32_t k;
    uint64_t t, held;
} msr_echo;

typedef struct {
    uint64_t t_us; /* the sender's clock at the send (µs; echoed back) */
    uint32_t cpus, big, little, ram_mb, disk_free_mb;
    uint32_t heap_kb, slots_free; /* the view heap's largest free run (KiB) and the free slots */
    msr_prog *progs;
    size_t nprogs;
    msr_fail *fails;
    size_t nfails;
    msr_rtt *rtt;
    size_t nrtt;
    msr_echo *echo;
    size_t necho;
} msr_report;

void msr_add_prog(msr_report *r, const char *name, const msm_perf *p);
void msr_add_fail(msr_report *r, const char *name, uint64_t hash, bool unsupported, const char *why);
void msr_add_rtt(msr_report *r, uint32_t k, float us);
void msr_add_echo(msr_report *r, uint32_t k, uint64_t t, uint64_t held);
char *msr_encode(const msr_report *r);
bool msr_decode(const char *text, msr_report *out, char **err);
bool msr_eq(const msr_report *a, const msr_report *b);
void msr_free(msr_report *r);

#endif
