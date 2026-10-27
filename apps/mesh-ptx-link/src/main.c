/* mesh-ptx-link: links LLVM bitcode into one PTX module in the cluster float
 * mode (f32 FTZ, unfused), keeping only the exported symbols.
 *
 *   mesh-ptx-link --mcpu sm_86 [-O0..3] [--debug] [--export SYM]... -o OUT.ptx IN.bc...
 *
 * As alpha-rust-mesh scripts/ptx-link.sh (rustc's linker for programs): the
 * commands of rustc's llvm-bitcode-linker (llvm-link, opt, llc) with the float
 * mode added. Tools: $MESH_LLVM_LINK, $MESH_OPT, $MESH_LLC, or found on PATH
 * (also name-21 .. name-15) or under /usr/lib/llvm-N/bin. Any argument it
 * does not know is refused. */
#include "link.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int run(char **argv) {
    pid_t pid = fork();
    if (pid < 0) { fprintf(stderr, "mesh-ptx-link: fork: %s\n", strerror(errno)); return 1; }
    if (pid == 0) {
        execv(argv[0], argv);
        fprintf(stderr, "mesh-ptx-link: %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR) return 1;
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return 0;
    fprintf(stderr, "mesh-ptx-link: %s failed\n", argv[0]);
    return 1;
}

static int usage(void) {
    fprintf(stderr, "usage: mesh-ptx-link --mcpu sm_NN [-O0..3] [--debug] [--export SYM]... -o OUT.ptx IN.bc...\n");
    return 2;
}

int main(int argc, char **argv) {
    const char **inputs = calloc((size_t)argc, sizeof *inputs), **exports = calloc((size_t)argc, sizeof *exports);
    if (!inputs || !exports) return 1;
    mesh_ptx_link l = {.opt = 3};
    size_t ni = 0, ne = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((!strcmp(a, "--mcpu") || !strcmp(a, "--target-cpu")) && i + 1 < argc) l.cpu = argv[++i];
        else if ((!strcmp(a, "-o") || !strcmp(a, "--output")) && i + 1 < argc) l.output = argv[++i];
        else if (!strcmp(a, "--export") && i + 1 < argc) exports[ne++] = argv[++i];
        else if (!strcmp(a, "--debug")) l.debug = true;
        else if (a[0] == '-' && a[1] == 'O' && a[2] >= '0' && a[2] <= '3' && !a[3]) l.opt = a[2] - '0';
        else if (a[0] == '-') { fprintf(stderr, "mesh-ptx-link: unknown option %s\n", a); return usage(); }
        else inputs[ni++] = a;
    }
    if (!l.output || !l.cpu || ni == 0) { fprintf(stderr, "mesh-ptx-link: need -o, --mcpu and inputs\n"); return usage(); }
    if (!mesh_ptx_link_valid_arch(l.cpu)) { fprintf(stderr, "mesh-ptx-link: --mcpu %s: want sm_NN (e.g. sm_86, sm_121)\n", l.cpu); return 1; }
    l.inputs = inputs;
    l.ninputs = ni;
    if (ne) { l.exports = exports; l.nexports = ne; }
    char *llvm_link = mesh_ptx_link_find_tool("llvm-link", "MESH_LLVM_LINK");
    char *opt = mesh_ptx_link_find_tool("opt", "MESH_OPT");
    char *llc = mesh_ptx_link_find_tool("llc", "MESH_LLC");
    const char *missing = !llvm_link ? "llvm-link ($MESH_LLVM_LINK)" : !opt ? "opt ($MESH_OPT)" : !llc ? "llc ($MESH_LLC)" : NULL;
    if (missing) { fprintf(stderr, "mesh-ptx-link: no %s: install LLVM's tools\n", missing); return 1; }
    l.llvm_link = llvm_link;
    l.opt_tool = opt;
    l.llc = llc;
    char *syms = mesh_ptx_link_symbols(&l), *symfile = mesh_ptx_link_side(&l, ".symbols.txt");
    FILE *f = fopen(symfile, "w");
    if (!f || fputs(syms, f) < 0 || fclose(f) != 0) { fprintf(stderr, "mesh-ptx-link: %s: %s\n", symfile, strerror(errno)); return 1; }
    char **steps[3] = {mesh_ptx_link_argv_link(&l), mesh_ptx_link_argv_opt(&l), mesh_ptx_link_argv_llc(&l)};
    int rc = 0;
    for (int i = 0; i < 3 && rc == 0; i++) rc = run(steps[i]);
    for (int i = 0; i < 3; i++) mesh_ptx_link_free_argv(steps[i]);
    free(syms), free(symfile), free(llvm_link), free(opt), free(llc), free(inputs), free(exports);
    return rc;
}
