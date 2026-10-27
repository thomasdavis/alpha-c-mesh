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

static msn_needs needs(const char *b0, const char *b1, const char *b2) {
    msn_needs n = msn_default();
    mt_strs_free(&n.backends);
    if (b0) mt_push(&n.backends, b0);
    if (b1) mt_push(&n.backends, b1);
    if (b2) mt_push(&n.backends, b2);
    return n;
}

static msn_term term(msn_term_kind k, const char *s, uint32_t sys) { return msn_term_new(k, s, sys); }

#endif
