/* The commands of the cluster-float-mode PTX link: llvm-link, opt, llc. */
#ifndef MESH_PTX_LINK_H
#define MESH_PTX_LINK_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *const *inputs; /* LLVM bitcode (or IR) files */
    size_t ninputs;
    const char *const *exports; /* symbols kept public; NULL: mesh_main, MESH_RT_ABI */
    size_t nexports;
    const char *output; /* the .ptx */
    const char *cpu;    /* sm_NN */
    int opt;            /* 0..3 */
    bool debug;         /* keep debug info */
    const char *llvm_link, *opt_tool, *llc; /* the tools */
} mesh_ptx_link;

/* The three commands, each a NULL-terminated argv the caller frees with
 * mesh_ptx_link_free_argv; they use <output without .ptx>.o, .optimized.o and
 * .symbols.txt beside the output, as the Rust linker script. */
char **mesh_ptx_link_argv_link(const mesh_ptx_link *l);
char **mesh_ptx_link_argv_opt(const mesh_ptx_link *l);
char **mesh_ptx_link_argv_llc(const mesh_ptx_link *l);
void mesh_ptx_link_free_argv(char **argv);
/* The symbols file's text: one exported symbol a line. Caller frees. */
char *mesh_ptx_link_symbols(const mesh_ptx_link *l);
/* "<output without .ptx>" + suffix. Caller frees. */
char *mesh_ptx_link_side(const mesh_ptx_link *l, const char *suffix);
/* An LLVM tool: $env if set, else name on PATH, else name-21 .. name-15 on
 * PATH, else the newest /usr/lib/llvm-N/bin/name. NULL when none exists. Caller frees. */
char *mesh_ptx_link_find_tool(const char *name, const char *env);
/* true for sm_NN, sm_NNN, with an optional a / f (MESH_PTX_ARCH's rule). */
bool mesh_ptx_link_valid_arch(const char *arch);

#endif
