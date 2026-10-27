/* 5696 manifests against the Rust: every program.toml of alpha-rust-mesh, the
 * Rust's own test cases, and systematic mutations of every key (one case per
 * distinct verdict).  For each, tests/data/manifests.rust holds what the Rust
 * printed: "ERR <message>" or "OK <fnv of {:?}> <fnv of the entry> <fnv of the
 * extension> <threads> <smem> <native>" plus the place parts. */
#include "mesh/check.h"
#include "mesh/manifest_encode.h"

#include "mesh/abi/fnv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }
    mesh_text t = {0};
    char buf[65536];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) mesh_text_putn(&t, buf, k);
    fclose(f);
    *n = t.len;
    return mesh_text_take(&t);
}

static void regions(mesh_text *t, const mesh_manifest_region *r, size_t n) {
    mesh_text_putc(t, '[');
    for (size_t i = 0; i < n; i++) mesh_text_printf(t, "%s(%u, %u)", i ? ", " : "", (unsigned)r[i].off, (unsigned)r[i].len);
    mesh_text_putc(t, ']');
}

static char *verdict(const char *in, size_t n) {
    mesh_manifest m;
    char *err = NULL;
    mesh_text t = {0};
    if (!mesh_manifest_parse(in, n, &m, &err)) {
        mesh_text_printf(&t, "ERR %s", err);
        free(err);
        return mesh_text_take(&t);
    }
    mesh_text d = {0};
    mesh_manifest_debug(&d, &m);
    mesh_image im = {0x100000, 5, UINT64_C(0x1122334455667788), 40, 9000, 6, 1};
    uint8_t e[MESH_PT_BYTES], x[MESH_PX_BYTES];
    mesh_manifest_encode_entry(&m, &im, e);
    mesh_manifest_encode_ext(&m, x);
    mesh_text_printf(&t, "OK %016llx %016llx %016llx %u %u %s",
                     (unsigned long long)mesh_fnv1a64((const uint8_t *)mesh_text_str(&d), d.len),
                     (unsigned long long)mesh_fnv1a64(e, sizeof e), (unsigned long long)mesh_fnv1a64(x, sizeof x),
                     (unsigned)mesh_manifest_threads(&m), (unsigned)mesh_manifest_smem(&m), mesh_manifest_native(&m) ? "true" : "false");
    if (m.place) {
        const mesh_manifest_place *p = m.place;
        mesh_text_printf(&t, " place %zu %u %zu", mesh_manifest_place_state_bytes(p), (unsigned)mesh_manifest_place_out_bytes(p),
                         mesh_manifest_place_part_bytes(p));
        mesh_manifest_region *r = mesh_alloc(p->nout * sizeof *r);
        for (uint32_t b = 0; b < p->fanout; b++) {
            mesh_text_putc(&t, ' ');
            mesh_manifest_place_part(p, b, r);
            regions(&t, r, p->nout);
            mesh_manifest_place_part_in_buffer(p, b, r);
            regions(&t, r, p->nout);
        }
        free(r);
    }
    /* A copy is the same manifest. */
    mesh_manifest c;
    mesh_manifest_copy(&c, &m);
    mesh_text d2 = {0};
    mesh_manifest_debug(&d2, &c);
    CHECK_S(mesh_text_str(&d2), mesh_text_str(&d));
    mesh_text_free(&d2);
    mesh_manifest_free(&c);
    if (getenv("MESH_GOLDEN_DEBUG")) fprintf(stderr, "%s\n", mesh_text_str(&d));
    mesh_text_free(&d);
    mesh_manifest_free(&m);
    return mesh_text_take(&t);
}

int main(void) {
    size_t ni, ne;
    char *ins = slurp("tests/data/manifests.in", &ni);
    char *exp = slurp("tests/data/manifests.rust", &ne);
    size_t cases = 0, bad = 0;
    const char *ip = ins, *ep = exp;
    for (;;) {
        const char *isep = memchr(ip, '\x1e', (size_t)(ins + ni - ip));
        size_t ilen = isep ? (size_t)(isep - ip) : (size_t)(ins + ni - ip);
        const char *nl = memchr(ep, '\n', (size_t)(exp + ne - ep));
        if (!nl) break;
        char *want = mesh_strndup(ep, (size_t)(nl - ep));
        char *got = verdict(ip, ilen);
        cases++;
        if (!CHECK(strcmp(got, want) == 0) && ++bad <= 25) {
            mesh_text qt = {0};
            mesh_text_debug_str(&qt, ip, ilen);
            char *q = mesh_text_take(&qt);
            fprintf(stderr, "case %zu: %s\n  rust: %s\n  c:    %s\n", cases - 1, q, want, got);
            free(q);
        }
        free(want);
        free(got);
        ep = nl + 1;
        if (!isep) break;
        ip = isep + 1;
    }
    CHECK_U(cases, 5696);
    printf("%zu manifests, %zu differ\n", cases, bad);
    free(ins);
    free(exp);
    return mesh_check_done();
}
