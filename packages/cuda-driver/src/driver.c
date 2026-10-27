/* The table, dlopen'd once, or injected. */
#include "mesh/cuda_driver.h"
#include "mesh/text.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

extern const mesh_cu_name mesh_cu_result_names[];
extern const size_t mesh_cu_result_names_len;

static mesh_cuda_fns loaded;
static const mesh_cuda_fns *current;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static bool tried, ok;
static char *load_err;

const mesh_cuda_fns *mesh_cuda(void) { return current; }
void mesh_cuda_set(const mesh_cuda_fns *fns) { current = fns; }
bool mesh_cuda_injected(void) { return current != NULL && current != &loaded; }

#define SYM(name)                                                                \
    do {                                                                         \
        void *p = dlsym(lib, #name);                                             \
        if (!p) {                                                                \
            load_err = mesh_fmt("cuda: libcuda.so.1 has no %s", #name);          \
            return false;                                                        \
        }                                                                        \
        memcpy(&loaded.name, &p, sizeof p);                                      \
    } while (0)

static bool load_all(void) {
    void *lib = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        const char *e = dlerror();
        load_err = mesh_fmt("cuda: cannot open libcuda.so.1: %s", e ? e : "");
        return false;
    }
    SYM(cuInit);
    SYM(cuGetErrorName);
    SYM(cuModuleGetLoadingMode);
    SYM(cuDeviceGet);
    SYM(cuDeviceGetName);
    SYM(cuDeviceGetAttribute);
    SYM(cuDevicePrimaryCtxRetain);
    SYM(cuCtxSetCurrent);
    SYM(cuCtxGetLimit);
    SYM(cuCtxSetLimit);
    SYM(cuDeviceGetDevResource);
    SYM(cuDevSmResourceSplitByCount);
    SYM(cuDevResourceGenerateDesc);
    SYM(cuGreenCtxCreate);
    SYM(cuCtxFromGreenCtx);
    SYM(cuGreenCtxStreamCreate);
    SYM(cuStreamCreate);
    SYM(cuStreamDestroy_v2);
    SYM(cuStreamQuery);
    SYM(cuStreamSynchronize);
    SYM(cuMemHostRegister_v2);
    SYM(cuMemHostGetDevicePointer_v2);
    SYM(cuModuleLoadData);
    SYM(cuModuleGetFunction);
    SYM(cuModuleUnload);
    SYM(cuModuleGetGlobal_v2);
    SYM(cuFuncGetAttribute);
    SYM(cuFuncSetAttribute);
    SYM(cuOccupancyMaxActiveBlocksPerMultiprocessor);
    SYM(cuMemAlloc_v2);
    SYM(cuMemFree_v2);
    SYM(cuMemsetD8Async);
    SYM(cuMemcpyDtoH_v2);
    SYM(cuLaunchKernel);
    return true;
}

bool mesh_cuda_load(char **err) {
    pthread_mutex_lock(&mu);
    if (!tried) {
        tried = true;
        ok = load_all();
        if (ok && !current) current = &loaded;
    }
    bool r = ok;
    if (!r) mesh_errf(err, "%s", load_err ? load_err : "cuda: not loaded");
    pthread_mutex_unlock(&mu);
    return r;
}

const char *mesh_cu_result_name(CUresult r) {
    for (size_t i = 0; i < mesh_cu_result_names_len; i++)
        if (mesh_cu_result_names[i].code == r) return mesh_cu_result_names[i].name;
    return NULL;
}

char *mesh_cu_result_debug(CUresult r) {
    const char *n = mesh_cu_result_name(r);
    return n ? mesh_strdup(n) : mesh_fmt("cudaError_enum(%d)", r);
}

bool mesh_cu_ck(CUresult r, const char *what, char **err) {
    if (r == CUDA_SUCCESS) return true;
    const char *s = NULL;
    if (current && current->cuGetErrorName) current->cuGetErrorName(r, &s);
    if (s) return mesh_errf(err, "%s: %s", what, s);
    char *d = mesh_cu_result_debug(r);
    mesh_errf(err, "%s: %s", what, d);
    free(d);
    return false;
}

bool mesh_cu_ck_debug(CUresult r, const char *what, char **err) {
    if (r == CUDA_SUCCESS) return true;
    char *d = mesh_cu_result_debug(r);
    mesh_errf(err, "%s: %s", what, d);
    free(d);
    return false;
}
