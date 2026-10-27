/* The builder's rules on temporary trees that need no tools: package ids,
 * the arch rule, sexc.json fields; a wrong manifest name refused; a wasm-only
 * program (wasm images skipped) gets its manifest and its variants; a native
 * program for another OS gets its manifest and source.hash but no binary; a
 * variant with the wrong name refused. */
#include "mesh/check.h"

#include "../src/build.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/mesh-build-programs-XXXXXX";

static void put(const char *rel, const char *text) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    for (char *s = p + strlen(root) + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    FILE *f = fopen(p, "w");
    fputs(text, f);
    fclose(f);
}
static bool exists(const char *rel) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    return access(p, F_OK) == 0;
}

int main(void) {
    char *id = mbp_pkg_id("@ajax/mesh-prog-hello");
    CHECK_S(id, "ajax-mesh-prog-hello");
    free(id);
    CHECK(mbp_valid_arch("sm_86") && mbp_valid_arch("sm_121") && mbp_valid_arch("sm_90a"));
    CHECK(!mbp_valid_arch("sm_8") && !mbp_valid_arch("86") && !mbp_valid_arch("sm_86b"));
    char *f = mbp_json_field("{\n  \"name\": \"@ajax/x\",\n  \"kind\" : \"ptx\"\n}", "kind");
    CHECK_S(f, "ptx");
    free(f);
    CHECK(mbp_json_field("{\"name\": 3}", "name") == NULL);
    CHECK(strlen(mbp_triple()) > 0);

    CHECK(mkdtemp(root) != NULL);
    char out[600];
    snprintf(out, sizeof out, "%s/out", root);
    mbp_opts o = {.root = root, .out = out, .arch = "sm_86", .sexc = "/nonexistent/sexc", .ptx_link = "/nonexistent/link", .skip_wasm = true};
    /* A wasm-only program and its variant. */
    put("programs/pipesrc-wasm/program.toml", "name = \"pipesrc\"\nbackends = [\"wasm\"]\n");
    put("programs/pipesrc-wasm/sexc.json", "{\"name\": \"@ajax/mesh-prog-pipesrc-wasm\", \"kind\": \"wasm\"}\n");
    put("programs/pipesrc-wasm/variants/pipesrc-b.toml", "name = \"pipesrc-b\"\nbackends = [\"wasm\"]\n");
    /* A native program built on another OS only. */
    put("programs/ane/program.toml", "# build-os: Plan9\nname = \"ane\"\nbackends = [\"native\"]\n");
    put("programs/ane/sexc.json", "{\"name\": \"@ajax/mesh-prog-ane\", \"kind\": \"executable\"}\n");
    put("programs/ane/src/main.c", "int main(void) { return 0; }\n");
    put("packages/mesh-abi-x/include/x.h", "int x;\n");
    char *names[] = {(char *)"pipesrc", (char *)"ane"};
    CHECK_I(mbp_run(&o, names, 2), 0);
    CHECK(exists("out/pipesrc/program.toml"));
    CHECK(!exists("out/pipesrc/pipesrc.wasm"));
    CHECK(exists("out/pipesrc-b/program.toml"));
    CHECK(exists("out/ane/program.toml") && exists("out/ane/source.hash"));
    char tb[256];
    snprintf(tb, sizeof tb, "out/ane/%s/ane", mbp_triple());
    CHECK(!exists(tb));
    /* A GPU program with no tools: the build fails, it does not invent an image. */
    put("programs/hello/program.toml", "name = \"hello\"\nbackends = [\"cuda\"]\n");
    put("programs/hello/sexc.json", "{\"name\": \"@ajax/mesh-prog-hello\", \"kind\": \"ptx\"}\n");
    char *h[] = {(char *)"hello"};
    CHECK_I(mbp_run(&o, h, 1), 1);
    CHECK(!exists("out/hello/hello.ptx"));
    /* A bad arch, a wrong variant name, then a wrong manifest name. */
    mbp_opts bad = o;
    bad.arch = "sm_8";
    CHECK_I(mbp_run(&bad, names, 1), 1);
    put("programs/pipesrc-wasm/variants/oops.toml", "name = \"other\"\n");
    CHECK_I(mbp_run(&o, names, 1), 1);
    put("programs/wrong/program.toml", "name = \"right\"\n");
    put("programs/wrong/sexc.json", "{\"name\": \"@ajax/x\", \"kind\": \"ptx\"}\n");
    CHECK_I(mbp_run(&o, names, 1), 1);
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    CHECK(system(cmd) == 0);
    return mesh_check_done();
}
