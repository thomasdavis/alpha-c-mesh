/* loader.rs's tests (names_only, confined_and_catalog) and the hashes. */
#include "mesh/check.h"
#include "mesh/loader.h"

#include "mesh/abi/fnv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void write_file(const char *path, const char *s) {
    FILE *f = fopen(path, "w");
    fputs(s, f);
    fclose(f);
}

static void rm_rf(const char *dir) {
    char *cmd[] = {"rm", "-rf", (char *)dir, NULL};
    pid_t p = fork();
    if (p == 0) {
        execvp("rm", cmd);
        _exit(127);
    }
    int st;
    waitpid(p, &st, 0);
}

int main(void) {
    /* names_only */
    const char *good[][2] = {{"hello", "hello"}, {"hello/program.toml", "hello"}, {"hello-v2/", "hello-v2"}};
    for (size_t i = 0; i < 3; i++) {
        char *n = mesh_catalog_name(good[i][0], NULL);
        CHECK_S(n, good[i][1]);
        free(n);
    }
    const char *bad[] = {"", "../etc", "/etc/passwd", "a/b", "Hello", "a b", "x/program.toml/..", ".", "hello/../x"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) CHECK(mesh_catalog_name(bad[i], NULL) == NULL);
    char *err = NULL;
    CHECK(!mesh_catalog_name("../etc", &err));
    CHECK_S(err, "manifest: \"../etc\": programs are named [a-z0-9-] under --programs (no paths)");
    free(err);
    err = NULL;

    /* confined_and_catalog */
    char root[] = "/tmp/meshd-loader-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char *p = mesh_fmt("%s/progs", root);
    char *a = mesh_fmt("%s/progs/a", root), *b = mesh_fmt("%s/progs/b", root), *out = mesh_fmt("%s/outside", root);
    mkdir(p, 0755);
    mkdir(a, 0755);
    mkdir(b, 0755);
    mkdir(out, 0755);
    char *toml = mesh_fmt("%s/program.toml", a);
    write_file(toml, "name = \"a\"\nabi = 6\n");
    char *esc = mesh_fmt("%s/progs/esc", root);
    CHECK(symlink(out, esc) == 0);
    char *r = mesh_loader_resolve(p, "a", &err);
    CHECK(r != NULL);
    free(r);
    CHECK(mesh_loader_resolve(p, "esc", &err) == NULL);
    CHECK(err && strstr(err, "not a program dir"));
    free(err);
    err = NULL;
    CHECK(mesh_loader_resolve(p, "nope", &err) == NULL);
    CHECK_S(err, "manifest: nope: No such file or directory (os error 2)");
    free(err);
    err = NULL;
    char **names;
    size_t n;
    CHECK(mesh_catalog(p, &names, &n, &err));
    CHECK_U(n, 1);
    if (n == 1) CHECK_S(names[0], "a");
    for (size_t i = 0; i < n; i++) free(names[i]);
    free(names);

    /* read: a CUDA load needs the PTX; without need_ptx it is left empty. */
    mesh_payload pl;
    CHECK(!mesh_loader_read(p, "a", true, &pl, &err));
    char *want = mesh_fmt("manifest: %s/a.ptx (a): No such file or directory (os error 2)", a);
    char real[4096];
    if (realpath(a, real)) {
        free(want);
        want = mesh_fmt("manifest: %s/a.ptx (a): No such file or directory (os error 2)", real);
    }
    CHECK_S(err, want);
    free(want);
    free(err);
    err = NULL;
    CHECK(mesh_loader_read(p, "a", false, &pl, &err));
    CHECK_S(pl.dir, "a");
    CHECK_U(pl.ptx_len, 0);
    CHECK_S(pl.manifest, "name = \"a\"\nabi = 6\n");
    mesh_payload_free(&pl);
    char *ptx = mesh_fmt("%s/a.ptx", a);
    write_file(ptx, ".version 8.8\n");
    CHECK(mesh_loader_read(p, "a/program.toml", true, &pl, &err));
    CHECK_U(pl.ptx_len, 13);
    mesh_manifest m;
    uint64_t h;
    mesh_payload_free(&pl);
    CHECK(mesh_loader_identity(p, "a", &pl, &m, &h, &err));
    CHECK_U(h, mesh_fnv1a64((const uint8_t *)".version 8.8\n", 13));
    mesh_manifest_free(&m);
    mesh_payload_free(&pl);

    /* Hashes: param folded in; the native identity trims source.hash. */
    const uint8_t code[] = "code";
    CHECK_U(mesh_image_hash(code, 4, 0), mesh_fnv1a64(code, 4));
    const uint8_t with[] = {'c', 'o', 'd', 'e', 0, 'p', 'a', 'r', 'a', 'm', '=', 14, 0, 0, 0};
    CHECK_U(mesh_image_hash(code, 4, 14), mesh_fnv1a64(with, sizeof with));
    uint64_t ida = mesh_native_identity("name = \"g\"\nabi = 2\n", "00ff", 0);
    CHECK_U(ida, mesh_native_identity("name = \"g\"\nabi = 2\n", "00ff\n", 0));
    CHECK(ida != mesh_native_identity("name = \"g\"\nabi = 2\n", "00fe", 0));
    CHECK(ida != mesh_native_identity("name = \"g\"\nabi = 2\nnodes = 2\n", "00ff", 0));
    CHECK(ida != mesh_native_identity("name = \"g\"\nabi = 2\n", "00ff", 1));
    uint64_t e = 5;
    CHECK(mesh_check_hash("x", "PTX", 5, &e, &err));
    CHECK(mesh_check_hash("x", "PTX", 5, NULL, &err));
    e = 6;
    CHECK(!mesh_check_hash("x", "PTX", 5, &e, &err));
    CHECK_S(err, "peer: x: the PTX received hashes to 0000000000000005, the peer announced 0000000000000006");
    free(err);

    rm_rf(root);
    free(p);
    free(a);
    free(b);
    free(out);
    free(toml);
    free(esc);
    free(ptx);
    return mesh_check_done();
}
