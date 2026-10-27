/* The Rust tests' fleet: dgx-00, gx10-9959, being (x86), the Mac (Metal) (sched_tests.rs member/fleet4). */
#ifndef FIXTURE_H
#define FIXTURE_H

#include "mesh/sched_member.h"
#include "mesh/sched_needs.h"

#include <string.h>

static msm_member member(uint32_t id, const char *gpu, const char *host) {
    msm_member m = msm_member_default();
    m.id = id;
    m.up = true;
    MSM_SET(m.host, host);
    MSM_SET(m.gpu, gpu);
    MSM_SET(m.arch, strcmp(gpu, "cuda") == 0 && id == 2 ? "x86_64" : "aarch64");
    m.cpus = 20;
    m.ram_mb = 120000;
    m.mem_avail_mb = 80000;
    m.gpu_share = mof32_some(0.5f);
    m.cpu_util = 0.1f;
    m.psi_cpu = mof32_some(0.01f);
    m.psi_mem = mof32_some(0.0f);
    m.load_known = true;
    m.nrtt = 4;
    for (int i = 0; i < 4; i++) m.rtt_us[i] = mof32_some(300.0f);
    m.heap_kb = 7000;
    m.slots_free = 28;
    m.max_threads = 1024;
    m.max_shared = strcmp(gpu, "metal") == 0 ? 32768 : 101376;
    return m;
}

static void fleet4(msm_member ms[4]) {
    ms[0] = member(0, "cuda", "dgx-00");
    ms[1] = member(1, "cuda", "gx10-9959");
    ms[2] = member(2, "cuda", "being");
    ms[3] = member(3, "metal", "monumentals-mac-studio.local");
}

static mesh_needs needs(const char *b0, const char *b1, const char *b2) {
    mesh_needs n = msn_default();
    mesh_strs_free(n.backends, n.nbackends), n.backends = NULL, n.nbackends = 0;
    if (b0) msn_strs_push(&n.backends, &n.nbackends, b0);
    if (b1) msn_strs_push(&n.backends, &n.nbackends, b1);
    if (b2) msn_strs_push(&n.backends, &n.nbackends, b2);
    return n;
}

static mesh_term term(mesh_term_kind k, const char *s, uint32_t sys) { return msn_term_new(k, s, sys); }

#endif

#include "mesh/fleet_doc.h"
#include "mesh/sched_place.h"

#include <stdlib.h>

/* sched_tests.rs prog(): a program of these backends, view 4 KiB, needs adjusted by the caller. */
static mfd_prog prog_of(mesh_needs n) {
    if (!n.view_bytes) n.view_bytes = 0x1000;
    mfd_prog p = mfd_prog_new("x", 1, true, &n);
    p.lockstep = n.lockstep;
    for (size_t i = 0; i < n.nbackends; i++) mt_push(&p.backends, n.backends[i]);
    mesh_needs_free(&n);
    return p;
}

static mfd_doc doc_new(void) { return (mfd_doc){.version = 1, .term = 1}; }

/* place() with no measurements and the default weights: the document. */
static mfd_doc placed(const msm_member *ms, size_t n, const mfd_doc *d) {
    msm_perfs perf = {0};
    msw_weights w = msw_default();
    msp_input in = {d, ms, n, &perf, &w};
    mt_strs ch;
    msp_explains ex;
    mfd_doc out = msp_place(&in, &ch, &ex);
    mt_strs_free(&ch);
    msp_explains_free(&ex);
    return out;
}

static bool at_is(const mfd_doc *d, const char *name, const uint32_t *v, size_t n) {
    const mfd_prog *p = mfd_prog_get(d, name);
    if (!p || !p->at.some || p->at.l.n != n) return false;
    for (size_t i = 0; i < n; i++)
        if (p->at.l.v[i] != v[i]) return false;
    return true;
}

static void free_members(msm_member *ms, size_t n) {
    for (size_t i = 0; i < n; i++) msm_member_free(&ms[i]);
}
