/* sched_tests.rs needs_round_trip_and_terms, and the error messages. */
#include "mesh/check.h"
#include "mesh/sched_needs.h"

#include <stdlib.h>

static msm_member member(uint32_t id, const char *gpu, const char *host, const char *arch) {
    msm_member m = msm_member_default();
    m.id = id;
    MSM_SET(m.gpu, gpu);
    MSM_SET(m.host, host);
    MSM_SET(m.arch, arch);
    return m;
}

static msn_term term(const char *s) {
    msn_term t;
    char *err = NULL;
    if (!msn_term_parse(s, &t, &err)) {
        fprintf(stderr, "%s\n", err);
        free(err);
        abort();
    }
    return t;
}

static void round_trip(const msn_needs *n) {
    char *e = msn_encode(n);
    CHECK(strchr(e, ' ') == NULL);
    msn_needs d;
    char *err = NULL;
    CHECK(msn_decode(e, &d, &err));
    CHECK(msn_eq(&d, n));
    msn_free(&d);
    free(e);
}

int main(void) {
    msn_needs n = msn_default();
    mt_strs_free(&n.backends);
    mt_push(&n.backends, "cuda");
    mt_push(&n.backends, "wasm");
    n.arch = mt_strdup("aarch64");
    n.min_mem_mb = 4096;
    msn_terms_push(&n.prefer, term("cuda"));
    msn_terms_push(&n.prefer, term("host:dgx-00"));
    msn_terms_push(&n.avoid, term("system:3"));
    n.replicas = (msn_replicas){false, 2};
    n.pack = true;
    n.colocate_with = mt_strdup("pipehash");
    mt_push(&n.feeds, "a");
    n.group = mt_strdup("g");
    n.group_size = 3;
    msn_terms_push(&n.group_members, term("arch:aarch64"));
    n.priority = -2;
    n.cpu = 0.5f;
    n.mem_mb = 64;
    n.migrate = MSN_RESTART;
    n.lockstep = true;
    n.view_bytes = 0x10000;
    n.warps = 8;
    n.shared = 0x3000;
    round_trip(&n);
    char *e = msn_encode(&n);
    CHECK_S(e, "b=cuda,wasm;ar=aarch64;mm=4096;pf=cuda,host:dgx-00;av=system:3;r=2;sp=pack;co=pipehash;fd=a;g=g;gs=3;gm=arch:aarch64;pr=-2;cpu=0.5;mem=64;mg=restart;ls=1;vb=65536;w=8;sh=12288");
    free(e);
    char *s = msn_summary(&n);
    CHECK_S(s, "replicas 2 pack backends cuda,wasm arch aarch64 min_mem_mb 4096 prefer cuda,host:dgx-00 avoid system:3 colocate_with pipehash group g size 3 members arch:aarch64 priority -2 cpu 0.50 mem_mb 64 view 64 KiB node 8 warps, 12288 shared bytes migrate restart");
    free(s);
    msn_free(&n);
    n = msn_default();
    round_trip(&n);
    e = msn_encode(&n);
    CHECK_S(e, "b=cuda;ar=-;mm=0;pf=-;av=-;r=all;sp=spread;co=-;fd=-;g=-;gs=0;gm=-;pr=0;cpu=0;mem=0;mg=state;ls=0;vb=0;w=1;sh=0");
    free(e);
    s = msn_summary(&n);
    CHECK_S(s, "replicas all backends cuda view 0 KiB migrate state");
    free(s);
    msn_free(&n);
    char *err = NULL;
    msn_needs d;
    CHECK(!msn_decode("cpu=nan", &d, &err));
    CHECK_S(err, "needs: cpu=\"nan\"");
    free(err);
    CHECK(!msn_decode("zz=1", &d, &err));
    CHECK_S(err, "needs: unknown key \"zz\"");
    free(err);
    CHECK(!msn_decode("w=0", &d, &err));
    CHECK_S(err, "needs: w=\"0\"");
    free(err);
    CHECK(!msn_decode("b", &d, &err));
    CHECK_S(err, "needs: \"b\"");
    free(err);
    msn_term t;
    CHECK(!msn_term_parse("host:", &t, &err));
    CHECK_S(err, "\"host:\": a backend (cuda, metal, wasm, native), \"host:NAME\", \"system:K\" or \"arch:A\"");
    free(err);
    CHECK(!msn_term_parse("gpu", &t, &err));
    free(err);
    CHECK(!msn_term_parse("system:9", &t, &err));
    free(err);
    msm_member mac = member(3, "metal", "monumentals-mac-studio.local", "aarch64");
    msm_member dgx = member(0, "cuda", "dgx-00", "aarch64");
    msm_member being = member(2, "cuda", "being", "x86_64");
    t = term("host:monumentals-mac-studio");
    CHECK(msn_term_matches(&t, &mac));
    free(t.s);
    t = term("metal");
    CHECK(msn_term_matches(&t, &mac) && !msn_term_matches(&t, &dgx));
    free(t.s);
    t = term("arch:x86_64");
    CHECK(msn_term_matches(&t, &being));
    free(t.s);
    t = term("system:2");
    CHECK(msn_term_matches(&t, &being) && !msn_term_matches(&t, &mac));
    char *w = msn_term_word(&t);
    CHECK_S(w, "system:2");
    free(w);
    return mesh_check_done();
}
