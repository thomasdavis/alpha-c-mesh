/* backend.rs's free functions: labels, pick_exec, check_manifest, prepare_any, cleanup order. */
#include "mesh/backend.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

static mesh_manifest parse(const char *t) {
    mesh_manifest m;
    char *err = NULL;
    if (!mesh_manifest_parse(t, strlen(t), &m, &err)) {
        fprintf(stderr, "%s\n", err);
        exit(1);
    }
    return m;
}

static int prepared_by;
static bool fake_needs_ptx(const mesh_compiler *c) { return *(const int *)c->self == 0; }
static bool fake_prepare(const mesh_compiler *c, mesh_payload *src, const uint64_t *expect, const char *root, mesh_prepared *out,
                         char **err) {
    (void)expect, (void)root, (void)err;
    prepared_by = *(const int *)c->self;
    *out = (mesh_prepared){0};
    out->src = *src;
    *src = (mesh_payload){0};
    out->exec = (uint32_t)prepared_by;
    char *e = NULL;
    return mesh_manifest_parse(out->src.manifest, strlen(out->src.manifest), &out->manifest, &e);
}
static const mesh_compiler_ops fake_ops = {fake_needs_ptx, fake_prepare};

static char order[8];
static void c_native(void) { strcat(order, "n"); }
static void c_mps(void) { strcat(order, "m"); }

int main(void) {
    char *l;
    CHECK_S(l = mesh_backend_label(mesh_backend_word(MESH_BACKEND_CUDA, 0)), "cuda");
    free(l);
    CHECK_S(l = mesh_backend_label(mesh_backend_word(MESH_BACKEND_METAL, 9)), "metal-apple9");
    free(l);
    CHECK_S(l = mesh_backend_label(mesh_backend_word(MESH_BACKEND_METAL, 0)), "metal");
    free(l);
    CHECK_S(l = mesh_backend_label(mesh_backend_word(MESH_BACKEND_WASM, 0)), "wasm");
    free(l);
    CHECK_S(l = mesh_backend_label(7), "backend7");
    free(l);

    /* pick_exec: the manifest's first backend this meshd runs. */
    mesh_manifest m = parse("name = \"p\"\nabi = 6\nbackends = [\"wasm\", \"cuda\"]");
    uint32_t have[] = {MESH_BACKEND_CUDA, MESH_BACKEND_WASM, MESH_BACKEND_NATIVE}, exec = 99;
    CHECK(mesh_pick_exec(&m, have, 3, &exec, NULL) && exec == MESH_BACKEND_WASM);
    char *err = NULL;
    CHECK(!mesh_pick_exec(&m, (uint32_t[]){MESH_BACKEND_NATIVE}, 1, &exec, &err));
    CHECK_S(err, "unsupported: p runs on wasm, cuda (manifest `backends`); this meshd runs native");
    free(err);
    err = NULL;
    mesh_manifest_free(&m);

    /* check_manifest: abi, backends, sms. */
    mesh_payload src = {"p", "name = \"p\"\nabi = 5", NULL, 0};
    CHECK(!mesh_check_manifest(&src, MESH_BACKEND_CUDA, &m, &err));
    CHECK_S(err, "abi: p declares abi 5, meshd runs 6");
    src.manifest = "name = \"p\"\nabi = 6\nbackends = [\"wasm\"]";
    CHECK(!mesh_check_manifest(&src, MESH_BACKEND_CUDA, &m, &err));
    CHECK_S(err, "unsupported: p runs on wasm (manifest `backends`); this meshd runs cuda");
    src.manifest = "name = \"p\"\nabi = 6\nsms = 2";
    CHECK(!mesh_check_manifest(&src, MESH_BACKEND_CUDA, &m, &err));
    CHECK_S(err, "manifest: p: sms = 2: per-program green partitions are not implemented");
    src.manifest = "name = \"p\"\nabi = 6\nbogus = 1";
    CHECK(!mesh_check_manifest(&src, MESH_BACKEND_CUDA, &m, &err));
    CHECK_S(err, "manifest: p: line 3: unknown key \"bogus\"");
    src.manifest = "name = \"p\"\nabi = 6";
    CHECK(mesh_check_manifest(&src, MESH_BACKEND_CUDA, &m, &err));
    mesh_manifest_free(&m);
    free(err);
    err = NULL;

    /* prepare_any: the executor's compiler; a PTX compiler refuses a catalog without PTX. */
    int k_cuda = 0, k_wasm = 2;
    mesh_compiler cuda = {&fake_ops, &k_cuda}, wasm = {&fake_ops, &k_wasm};
    mesh_compiler_entry comps[] = {{MESH_BACKEND_CUDA, &cuda}, {MESH_BACKEND_WASM, &wasm}};
    mesh_payload p = {mesh_strdup("w"), mesh_strdup("name = \"w\"\nabi = 6\nbackends = [\"wasm\"]"), NULL, 0};
    mesh_prepared pr;
    CHECK(mesh_prepare_any(comps, 2, &p, NULL, "/x", &pr, &err));
    CHECK_I(prepared_by, 2);
    CHECK_S(mesh_prepared_dir(&pr), "w");
    mesh_prepared_free(&pr);
    p = (mesh_payload){mesh_strdup("c"), mesh_strdup("name = \"c\"\nabi = 6"), NULL, 0};
    CHECK(!mesh_prepare_any(comps, 2, &p, NULL, "/x", &pr, &err));
    CHECK_S(err, "manifest: c/c.ptx (c): no PTX in the catalog (the cuda image)");
    free(err);

    /* Cleanups: native before MPS, whatever the registration order; once each. */
    mesh_backend_on_cleanup(MESH_CLEANUP_MPS, c_mps);
    mesh_backend_on_cleanup(MESH_CLEANUP_NATIVE, c_native);
    mesh_backend_on_cleanup(MESH_CLEANUP_NATIVE, c_native);
    mesh_backend_cleanup();
    CHECK_S(order, "nm");

    char *bin = mesh_native_binary("/progs/g", "g");
    char *want = mesh_fmt("/progs/g/%s/g", mesh_native_target());
    CHECK_S(bin, want);
    free(bin);
    free(want);
    return mesh_check_done();
}
