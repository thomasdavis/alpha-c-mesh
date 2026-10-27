/* mesh-build-programs [name...]: builds every mesh program (programs/<name>/)
 * into out/programs/<name>/ -- <name>.ptx, <name>.wasm, <triple>/<name> with
 * source.hash, program.toml -- and their variants.
 *
 *   MESH_PROGRAMS_OUT=DIR  the catalog (default <root>/out/programs)
 *   MESH_PTX_ARCH=sm_NN    the PTX target (default sm_86; meshd JITs it for its GPU)
 *   MESH_WASM_ONLY=1       only the wasm images;  MESH_NATIVE_ONLY=1  only the native programs
 *   MESH_SKIP_WASM=1       no wasm images (a catalog for CUDA and native members only)
 *   SEXC, MESH_PTX_LINK    the tools (default: sexc and mesh-ptx-link beside this binary, else on PATH)
 *   -C DIR                 the workspace (default: the current folder)
 *
 * Ported from alpha-rust-mesh scripts/build-programs.sh. */
#include "build.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool on(const char *k) {
    const char *v = getenv(k);
    return v && strcmp(v, "1") == 0;
}

/* A tool: $env, else beside this program (/proc/self/exe), else its name on PATH. */
static const char *tool(const char *env, const char *name, char *buf, size_t cap) {
    const char *v = getenv(env);
    if (v && *v) return v;
    char self[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n > 0) {
        self[n] = 0;
        char *slash = strrchr(self, '/');
        if (slash) {
            snprintf(buf, cap, "%.*s/%s", (int)(slash - self), self, name);
            if (access(buf, X_OK) == 0) return buf;
        }
    }
    return name;
}

int main(int argc, char **argv) {
    const char *root = ".";
    int first = 1;
    if (argc > 2 && strcmp(argv[1], "-C") == 0) {
        root = argv[2];
        first = 3;
    }
    for (int i = first; i < argc; i++)
        if (argv[i][0] == '-') {
            fprintf(stderr, "usage: mesh-build-programs [-C DIR] [name...]\n");
            return 2;
        }
    char outbuf[PATH_MAX], sexcbuf[PATH_MAX], linkbuf[PATH_MAX];
    const char *out = getenv("MESH_PROGRAMS_OUT");
    if (!out || !*out) {
        snprintf(outbuf, sizeof outbuf, "%s/out/programs", root);
        out = outbuf;
    }
    const char *arch = getenv("MESH_PTX_ARCH");
    mbp_opts o = {
        .root = root,
        .out = out,
        .arch = arch && *arch ? arch : "sm_86",
        .sexc = tool("SEXC", "sexc", sexcbuf, sizeof sexcbuf),
        .ptx_link = tool("MESH_PTX_LINK", "mesh-ptx-link", linkbuf, sizeof linkbuf),
        .wasm_only = on("MESH_WASM_ONLY"),
        .native_only = on("MESH_NATIVE_ONLY"),
        .skip_wasm = on("MESH_SKIP_WASM"),
    };
    return mbp_run(&o, argv + first, (size_t)(argc - first));
}
