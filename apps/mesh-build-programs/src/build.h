/* mesh-build-programs: the program catalog from programs/<name>/. */
#ifndef MESH_BUILD_PROGRAMS_H
#define MESH_BUILD_PROGRAMS_H
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *root;      /* the workspace (holds programs/ and packages/) */
    const char *out;       /* the catalog: MESH_PROGRAMS_OUT, default <root>/out/programs */
    const char *arch;      /* MESH_PTX_ARCH, default sm_86 */
    const char *sexc;      /* the sexc to build with */
    const char *ptx_link;  /* mesh-ptx-link */
    bool wasm_only;        /* MESH_WASM_ONLY=1 */
    bool native_only;      /* MESH_NATIVE_ONLY=1 */
    bool skip_wasm;        /* MESH_SKIP_WASM=1: no wasm images (a GPU-only catalog) */
} mbp_opts;

/* Builds the named programs (all when n == 0). 0 ok, 1 on the first error (printed). */
int mbp_run(const mbp_opts *o, char *const *names, size_t n);

/* "@ajax/mesh-prog-hello" -> "ajax-mesh-prog-hello" (sexc's package id). Caller frees. */
char *mbp_pkg_id(const char *name);
/* MESH_PTX_ARCH's rule: sm_NN or sm_NNN, optionally a or f. */
bool mbp_valid_arch(const char *a);
/* This machine's target triple, as meshd's native executor names it. */
const char *mbp_triple(void);
/* A string field of a sexc.json ("name", "kind", "bin"); NULL when absent. Caller frees. */
char *mbp_json_field(const char *json, const char *key);
#endif
