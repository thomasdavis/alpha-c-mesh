/* @ajax/nvjitlink: nvJitLink, opened at runtime (dlopen), compiles a program's
 * PTX to a cubin outside the blink window (mesh-host nvjitlink.rs): the
 * driver's own JIT would run inside it, and module loads block while any
 * kernel of the context runs.  It needs no CUDA context, so it runs on any
 * thread.  There is no fallback to the driver JIT: a missing library is an
 * error and meshd refuses to start. */
#ifndef MESH_NVJITLINK_H
#define MESH_NVJITLINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESH_NVJITLINK_LIB "libnvJitLink.so.13"

/* The CUDA 13.0 entry points (the versioned symbols nvJitLink.h's inline wrappers call). */
typedef struct {
    int (*create)(void **h, uint32_t n, const char *const *opts);  /* __nvJitLinkCreate_13_0 */
    int (*destroy)(void **h);                                      /* __nvJitLinkDestroy_13_0 */
    int (*add_data)(void *h, int kind, const void *data, size_t n, const char *name); /* __nvJitLinkAddData_13_0 */
    int (*complete)(void *h);                                      /* __nvJitLinkComplete_13_0 */
    int (*cubin_size)(void *h, size_t *n);                         /* __nvJitLinkGetLinkedCubinSize_13_0 */
    int (*cubin)(void *h, void *out);                              /* __nvJitLinkGetLinkedCubin_13_0 */
    int (*err_size)(void *h, size_t *n);                           /* __nvJitLinkGetErrorLogSize_13_0 */
    int (*err_log)(void *h, void *out);                            /* __nvJitLinkGetErrorLog_13_0 */
} mesh_nvjitlink_fns;

typedef struct {
    char *path;
    uint32_t major, minor;
    mesh_nvjitlink_fns fns;
} mesh_nvjitlink;

/* dlopen dir/libnvJitLink.so.13 and resolve the entry points; version 13 only.
 * False with *err: "nvJitLink: cannot open /x/libnvJitLink.so.13: ... (set --nvjitlink DIR; there is no fallback)". */
bool mesh_nvjitlink_open(const char *dir, mesh_nvjitlink *out, char **err);
/* A linker over given functions (tests). */
void mesh_nvjitlink_from(const mesh_nvjitlink_fns *fns, mesh_nvjitlink *out);
/* PTX text -> cubin for arch ("sm_121"); reg_cap: -maxrregcount (0: the compiler's choice).
 * *cubin is malloc'd.  The error carries nvJitLink's error log. */
bool mesh_nvjitlink_ptx_to_cubin(const mesh_nvjitlink *j, const uint8_t *ptx, size_t n, const char *arch, uint32_t reg_cap,
                                 uint8_t **cubin, size_t *cubin_len, char **err);
void mesh_nvjitlink_free(mesh_nvjitlink *j);

#endif
