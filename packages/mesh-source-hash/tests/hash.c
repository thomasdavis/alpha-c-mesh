/* The identity of a small tree: the same as hashing "path\0content" in path
 * order by hand; non-sources and .sexc folders ignored; any change moves it. */
#include "mesh/check.h"
#include "mesh/source_hash.h"
#include "sha256_stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[] = "/tmp/mesh-source-hash-XXXXXX";

static void put(const char *rel, const char *text) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    FILE *f = fopen(p, "w");
    fputs(text, f);
    fclose(f);
}

int main(void) {
    CHECK(mkdtemp(root) != NULL);
    char d[512];
    const char *mk[] = {"prog", "prog/src", "prog/.sexc", "lib", "lib/include"};
    for (int i = 0; i < 5; i++) { snprintf(d, sizeof d, "%s/%s", root, mk[i]); mkdir(d, 0755); }
    put("prog/program.toml", "name = \"p\"\n");
    put("prog/src/a.c", "int a;\n");
    put("prog/README.md", "ignored\n");
    put("prog/.sexc/x.c", "ignored\n");
    put("lib/include/b.h", "int b;\n");
    put("lib/sexc.json", "{}\n");
    const char *dirs[] = {"prog", "lib", "missing"};
    char h[17];
    CHECK(mesh_source_hash(root, dirs, 3, h));
    /* By hand: lib/include/b.h, lib/sexc.json, prog/program.toml, prog/src/a.c. */
    sha256_stream s;
    sha256_stream_init(&s);
    const char *files[4][2] = {{"lib/include/b.h", "int b;\n"}, {"lib/sexc.json", "{}\n"}, {"prog/program.toml", "name = \"p\"\n"}, {"prog/src/a.c", "int a;\n"}};
    for (int i = 0; i < 4; i++) {
        sha256_stream_update(&s, files[i][0], strlen(files[i][0]) + 1);
        sha256_stream_update(&s, files[i][1], strlen(files[i][1]));
    }
    uint8_t dg[32];
    char hex[65];
    sha256_stream_final(&s, dg);
    sha256_stream_hex(dg, hex);
    hex[16] = 0;
    CHECK_S(h, hex);
    put("prog/src/a.c", "int a = 1;\n");
    char h2[17];
    CHECK(mesh_source_hash(root, dirs, 3, h2));
    CHECK(strcmp(h, h2) != 0);
    snprintf(d, sizeof d, "rm -rf %s", root);
    CHECK(system(d) == 0);
    return mesh_check_done();
}
