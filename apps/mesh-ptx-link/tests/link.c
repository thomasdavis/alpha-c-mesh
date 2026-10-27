/* The three commands, exactly; the symbols file; the arch rule; finding a tool. */
#include "mesh/check.h"

#include "../src/link.h"

#include <stdlib.h>

static char *join(char **argv) {
    static char buf[4096];
    buf[0] = 0;
    for (char **p = argv; *p; p++) {
        if (p != argv) strcat(buf, " ");
        strcat(buf, *p);
    }
    mesh_ptx_link_free_argv(argv);
    return buf;
}

int main(void) {
    const char *in[] = {"a.bc", "b.bc"};
    mesh_ptx_link l = {.inputs = in, .ninputs = 2, .output = "out/hello.ptx", .cpu = "sm_86", .opt = 3,
                       .llvm_link = "/t/llvm-link", .opt_tool = "/t/opt", .llc = "/t/llc"};
    CHECK_S(join(mesh_ptx_link_argv_link(&l)), "/t/llvm-link --ignore-non-bitcode a.bc b.bc -o out/hello.o");
    CHECK_S(join(mesh_ptx_link_argv_opt(&l)),
            "/t/opt out/hello.o -o out/hello.optimized.o --denormal-fp-math-f32=preserve-sign "
            "--internalize-public-api-file=out/hello.symbols.txt --passes=default<O3>,internalize,globaldce --strip-debug");
    CHECK_S(join(mesh_ptx_link_argv_llc(&l)),
            "/t/llc --denormal-fp-math-f32=preserve-sign --fp-contract=off --mcpu sm_86 out/hello.optimized.o -o out/hello.ptx");
    l.debug = true;
    l.opt = 0;
    CHECK_S(join(mesh_ptx_link_argv_opt(&l)),
            "/t/opt out/hello.o -o out/hello.optimized.o --denormal-fp-math-f32=preserve-sign "
            "--internalize-public-api-file=out/hello.symbols.txt --passes=default<O0>,internalize,globaldce");
    char *s = mesh_ptx_link_symbols(&l);
    CHECK_S(s, "mesh_main\nMESH_RT_ABI\n");
    free(s);
    const char *ex[] = {"k"};
    l.exports = ex;
    l.nexports = 1;
    s = mesh_ptx_link_symbols(&l);
    CHECK_S(s, "k\n");
    free(s);
    CHECK(mesh_ptx_link_valid_arch("sm_86"));
    CHECK(mesh_ptx_link_valid_arch("sm_121"));
    CHECK(mesh_ptx_link_valid_arch("sm_90a"));
    CHECK(mesh_ptx_link_valid_arch("sm_100f"));
    CHECK(!mesh_ptx_link_valid_arch("sm_8"));
    CHECK(!mesh_ptx_link_valid_arch("sm_1234"));
    CHECK(!mesh_ptx_link_valid_arch("compute_86"));
    CHECK(!mesh_ptx_link_valid_arch("sm_86x"));
    setenv("MESH_TEST_TOOL", "/some/where/llc", 1);
    char *t = mesh_ptx_link_find_tool("llc", "MESH_TEST_TOOL");
    CHECK_S(t, "/some/where/llc");
    free(t);
    t = mesh_ptx_link_find_tool("no-such-tool-anywhere", NULL);
    CHECK(t == NULL);
    return mesh_check_done();
}
