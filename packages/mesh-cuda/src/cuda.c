/* cuda.rs over the @ajax/cuda-driver table. */
#include "mesh/cuda.h"
#include "mesh/text.h"

#include "mesh/abi/base.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CU (mesh_cuda())
#define CK(call, what)                         \
    do {                                       \
        if (!mesh_cu_ck((call), (what), err)) \
            return false;                      \
    } while (0)

static char *os_error(int e) { return mesh_fmt("%s (os error %d)", strerror(e), e); }

bool mesh_cuda_force_eager(char **err) {
    const char *v = getenv(MESH_CUDA_MODULE_LOADING);
    if (!v) return setenv(MESH_CUDA_MODULE_LOADING, "EAGER", 1) == 0;
    if (strcmp(v, "EAGER") == 0) return true;
    char *q = mesh_debug_str(v);
    mesh_errf(err, "%s=%s: refusing to run; module loading must be EAGER (a lazy load blocks behind resident kernels)",
              MESH_CUDA_MODULE_LOADING, q);
    free(q);
    return false;
}

char *mesh_cuda_lib_dir(int argc, char **argv) {
    for (int i = 0; i < argc; i++)
        if (strcmp(argv[i], "--cuda-lib") == 0) return i + 1 < argc ? mesh_strdup(argv[i + 1]) : NULL;
    const char *e = getenv(MESH_CUDA_LIB_ENV);
    return e && *e ? mesh_strdup(e) : NULL;
}

bool mesh_cuda_use_lib(const char *dir, int argc, char **argv, char **err) {
    if (!dir) return true;
    char canon[PATH_MAX];
    if (!realpath(dir, canon)) {
        char *e = os_error(errno);
        mesh_errf(err, "%s / --cuda-lib %s: %s", MESH_CUDA_LIB_ENV, dir, e);
        free(e);
        return false;
    }
    char *lib = mesh_fmt("%s/libcuda.so.1", canon);
    bool exists = access(lib, F_OK) == 0;
    free(lib);
    if (!exists) return mesh_errf(err, "%s / --cuda-lib %s: no libcuda.so.1 there", MESH_CUDA_LIB_ENV, dir);
    const char *ld = getenv("LD_LIBRARY_PATH");
    if (!ld) ld = "";
    size_t first = strcspn(ld, ":");
    if (first == strlen(canon) && strncmp(ld, canon, first) == 0) return setenv(MESH_CUDA_LIB_ENV, canon, 1) == 0;
    if (getenv("MESH_CUDA_LIB_EXEC"))
        return mesh_errf(err, "%s: re-executed, but LD_LIBRARY_PATH still does not start with %s", MESH_CUDA_LIB_ENV, canon);
    char *path = *ld ? mesh_fmt("%s:%s", canon, ld) : mesh_strdup(canon);
    setenv("LD_LIBRARY_PATH", path, 1);
    free(path);
    setenv(MESH_CUDA_LIB_ENV, canon, 1);
    setenv("MESH_CUDA_LIB_EXEC", "1", 1);
    /* The resolved path, not /proc/self/exe: the process keeps its name. */
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n < 0) {
        char *e = os_error(errno);
        mesh_errf(err, "%s: current_exe: %s", MESH_CUDA_LIB_ENV, e);
        free(e);
        return false;
    }
    exe[n] = 0;
    char **args = mesh_alloc(((size_t)argc + 1) * sizeof *args);
    for (int i = 0; i < argc; i++) args[i] = argv[i];
    execv(exe, args);
    char *e = os_error(errno);
    mesh_errf(err, "%s: re-exec with LD_LIBRARY_PATH=%s: %s", MESH_CUDA_LIB_ENV, canon, e);
    free(e);
    free(args);
    return false;
}

char *mesh_cuda_check_lib(char **err) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) {
        char *e = os_error(errno);
        mesh_errf(err, "/proc/self/maps: %s", e);
        free(e);
        return NULL;
    }
    char line[4096];
    char *lib = NULL;
    while (!lib && fgets(line, sizeof line, f)) {
        /* the 6th whitespace-separated field is the path */
        char *save = NULL, *tok = strtok_r(line, " \t\n", &save);
        for (int i = 0; tok && i < 5; i++) tok = strtok_r(NULL, " \t\n", &save);
        if (!tok) continue;
        const char *slash = strrchr(tok, '/');
        if (strncmp(slash ? slash + 1 : tok, "libcuda.so", 10) == 0) lib = mesh_strdup(tok);
    }
    fclose(f);
    if (!lib) {
        mesh_errf(err, "no libcuda mapped after cuInit");
        return NULL;
    }
    const char *dir = getenv(MESH_CUDA_LIB_ENV);
    if (dir && *dir) {
        char canon[PATH_MAX];
        const char *c = realpath(dir, canon) ? canon : dir;
        const char *slash = strrchr(lib, '/');
        size_t pn = slash ? (size_t)(slash - lib) : 0;
        if (!(slash && strlen(c) == pn && strncmp(lib, c, pn) == 0)) {
            mesh_errf(err, "%s=%s, but the driver library mapped is %s", MESH_CUDA_LIB_ENV, dir, lib);
            free(lib);
            return NULL;
        }
    }
    return lib;
}

static bool check_eager(char **err) {
    int mode = CU_MODULE_LAZY_LOADING;
    CK(CU->cuModuleGetLoadingMode(&mode), "cuModuleGetLoadingMode");
    if (mode != CU_MODULE_EAGER_LOADING) {
        const char *name = mode == CU_MODULE_LAZY_LOADING ? "CU_MODULE_LAZY_LOADING" : "CUmoduleLoadingMode_enum(?)";
        return mesh_errf(err, "the driver loads modules %s; EAGER is required (%s must be EAGER before cuInit)", name,
                         MESH_CUDA_MODULE_LOADING);
    }
    return true;
}

static bool attr(CUdevice dev, int a, int *v, char **err) {
    CK(CU->cuDeviceGetAttribute(v, a, dev), "cuDeviceGetAttribute");
    return true;
}

bool mesh_cuda_limits_of(CUdevice dev, mesh_cuda_limits *out, char **err) {
    int t, r, s;
    if (!attr(dev, CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK, &t, err) ||
        !attr(dev, CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK, &r, err) ||
        !attr(dev, CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN, &s, err))
        return false;
    *out = (mesh_cuda_limits){(uint32_t)t, (uint32_t)r, (uint32_t)s};
    return true;
}

uint32_t mesh_cuda_reg_cap(const mesh_cuda_limits *l, uint32_t warps) {
    uint32_t threads = MESH_NODE_THREADS * (warps ? warps : 1);
    uint32_t cap = (l->regs_per_block / threads) / 8 * 8;
    return cap < 255 ? (cap > 16 ? cap : 16) : 0;
}

bool mesh_cuda_limits_check(const mesh_cuda_limits *l, const char *name, uint32_t warps, uint32_t smem, char **err) {
    uint32_t threads = MESH_NODE_THREADS * warps;
    if (threads > l->max_threads)
        return mesh_errf(err, "occupancy: %s: warps %u = %u threads per node; the device allows %u per block", name, (unsigned)warps,
                         (unsigned)threads, (unsigned)l->max_threads);
    if (smem > l->max_shared)
        return mesh_errf(err,
                         "occupancy: %s: %u bytes of shared memory per node (the runtime's %u + shared_bytes); the device allows %u per block",
                         name, (unsigned)smem, (unsigned)MESH_RT_SHARED_BYTES, (unsigned)l->max_shared);
    return true;
}

bool mesh_cuda_gpu_open(uint32_t sms, mesh_cuda_gpu *out, char **err) {
    *out = (mesh_cuda_gpu){0};
    const char *ml = getenv(MESH_CUDA_MODULE_LOADING);
    if (!ml || strcmp(ml, "EAGER") != 0)
        return mesh_errf(err, "%s is not EAGER: call cuda::force_eager() first", MESH_CUDA_MODULE_LOADING);
    if (!CU && !mesh_cuda_load(err)) return false;
    CK(CU->cuInit(0), "cuInit");
    if (!check_eager(err)) return false;
    if (!mesh_cuda_injected()) {
        char *lib = mesh_cuda_check_lib(err);
        if (!lib) return false;
        free(lib);
    }
    CUdevice dev = 0;
    CK(CU->cuDeviceGet(&dev, 0), "cuDeviceGet");
    char name[128] = {0};
    CK(CU->cuDeviceGetName(name, 128, dev), "cuDeviceGetName");
    int major, minor, smt, bps, ha;
    if (!attr(dev, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, &major, err) ||
        !attr(dev, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, &minor, err) ||
        !attr(dev, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, &smt, err) ||
        !attr(dev, CU_DEVICE_ATTRIBUTE_MAX_BLOCKS_PER_MULTIPROCESSOR, &bps, err) ||
        !attr(dev, CU_DEVICE_ATTRIBUTE_HOST_NATIVE_ATOMIC_SUPPORTED, &ha, err))
        return false;
    mesh_cuda_limits limits;
    if (!mesh_cuda_limits_of(dev, &limits, err)) return false;
    CUcontext ctx = NULL;
    CUgreenCtx green_ctx = NULL;
    uint32_t used = (uint32_t)smt;
    if (sms == 0) {
        CK(CU->cuDevicePrimaryCtxRetain(&ctx, dev), "cuDevicePrimaryCtxRetain");
        CK(CU->cuCtxSetCurrent(ctx), "cuCtxSetCurrent");
    } else {
        CUdevResource all, group, rest;
        memset(&all, 0, sizeof all);
        memset(&group, 0, sizeof group);
        memset(&rest, 0, sizeof rest);
        CK(CU->cuDeviceGetDevResource(dev, &all, CU_DEV_RESOURCE_TYPE_SM), "cuDeviceGetDevResource");
        unsigned int n = 1;
        /* IGNORE_SM_COSCHEDULING: 2-SM granularity. */
        CK(CU->cuDevSmResourceSplitByCount(&group, &n, &all, &rest, CU_DEV_SM_RESOURCE_SPLIT_IGNORE_SM_COSCHEDULING, sms),
           "cuDevSmResourceSplitByCount");
        if (n != 1) return mesh_errf(err, "green context: cannot split %u SMs off %d", (unsigned)sms, smt);
        used = group.u.sm.smCount;
        CUdevResourceDesc desc = NULL;
        CK(CU->cuDevResourceGenerateDesc(&desc, &group, 1), "cuDevResourceGenerateDesc");
        CK(CU->cuGreenCtxCreate(&green_ctx, desc, dev, CU_GREEN_CTX_DEFAULT_STREAM), "cuGreenCtxCreate");
        CK(CU->cuCtxFromGreenCtx(&ctx, green_ctx), "cuCtxFromGreenCtx");
        CK(CU->cuCtxSetCurrent(ctx), "cuCtxSetCurrent");
    }
    *out = (mesh_cuda_gpu){ctx, green_ctx, mesh_strdup(name), major, minor, (uint32_t)smt, used, sms != 0, (uint32_t)bps, ha != 0, limits};
    return true;
}

void mesh_cuda_gpu_free(mesh_cuda_gpu *g) {
    free(g->name);
    g->name = NULL;
}

bool mesh_cuda_make_current(const mesh_cuda_gpu *g, char **err) {
    CK(CU->cuCtxSetCurrent(g->ctx), "cuCtxSetCurrent");
    return true;
}

bool mesh_cuda_stack_limit(const mesh_cuda_gpu *g, size_t *out, char **err) {
    (void)g;
    *out = 0;
    CK(CU->cuCtxGetLimit(out, CU_LIMIT_STACK_SIZE), "cuCtxGetLimit(STACK_SIZE)");
    return true;
}

bool mesh_cuda_set_stack_limit(const mesh_cuda_gpu *g, size_t bytes, char **err) {
    (void)g;
    CK(CU->cuCtxSetLimit(CU_LIMIT_STACK_SIZE, bytes), "cuCtxSetLimit(STACK_SIZE)");
    return true;
}

bool mesh_cuda_host_register(const mesh_cuda_gpu *g, void *p, size_t bytes, uint64_t *dev, char **err) {
    (void)g;
    CK(CU->cuMemHostRegister_v2(p, bytes, CU_MEMHOSTREGISTER_PORTABLE | CU_MEMHOSTREGISTER_DEVICEMAP), "cuMemHostRegister");
    CUdeviceptr d = 0;
    CK(CU->cuMemHostGetDevicePointer_v2(&d, p, 0), "cuMemHostGetDevicePointer");
    *dev = d;
    return true;
}

bool mesh_cuda_load_cubin(const mesh_cuda_gpu *g, const uint8_t *cubin, const char *entry, mesh_cuda_kernel *out, char **err) {
    (void)g;
    CUmodule module = NULL;
    CK(CU->cuModuleLoadData(&module, cubin), "cuModuleLoadData(cubin)");
    CUfunction func = NULL;
    char *what = mesh_fmt("cuModuleGetFunction(%s)", entry);
    bool ok = mesh_cu_ck(CU->cuModuleGetFunction(&func, module, entry), what, err);
    free(what);
    if (!ok) {
        CU->cuModuleUnload(module);
        return false;
    }
    *out = (mesh_cuda_kernel){module, func, MESH_NODE_THREADS, MESH_RT_SHARED_BYTES, 0};
    return true;
}

static void stream_drop(mesh_cuda_stream *s) {
    if (s->s) CU->cuStreamDestroy_v2(s->s);
    s->s = NULL;
}

bool mesh_cuda_new_stream(const mesh_cuda_gpu *g, mesh_cuda_stream *out, char **err) {
    CUstream s = NULL;
    if (g->green_ctx) CK(CU->cuGreenCtxStreamCreate(&s, g->green_ctx, CU_STREAM_NON_BLOCKING, 0), "cuGreenCtxStreamCreate");
    else CK(CU->cuStreamCreate(&s, CU_STREAM_NON_BLOCKING), "cuStreamCreate");
    out->s = s;
    return true;
}

void mesh_cuda_stream_destroy(mesh_cuda_stream *s) { stream_drop(s); }

bool mesh_cuda_launch_on(const mesh_cuda_gpu *g, const mesh_cuda_stream *st, const mesh_cuda_kernel *k, uint32_t nodes,
                         void **params, char **err) {
    (void)g;
    CK(CU->cuLaunchKernel(k->func, nodes, 1, 1, k->threads, 1, 1, k->smem, st->s, params, NULL), "cuLaunchKernel");
    return true;
}

bool mesh_cuda_query(const mesh_cuda_gpu *g, const mesh_cuda_stream *st, bool *done, char **err) {
    (void)g;
    CUresult r = CU->cuStreamQuery(st->s);
    if (r == CUDA_SUCCESS) {
        *done = true;
        return true;
    }
    if (r == CUDA_ERROR_NOT_READY) {
        *done = false;
        return true;
    }
    return mesh_cu_ck(r, "program stream", err);
}

bool mesh_cuda_unload(const mesh_cuda_gpu *g, mesh_cuda_kernel *k, char **err) {
    (void)g;
    if (k->scratch) {
        CK(CU->cuMemFree_v2(k->scratch), "cuMemFree(scratch)");
        k->scratch = 0;
    }
    CK(CU->cuModuleUnload(k->module), "cuModuleUnload");
    return true;
}

static bool func_attr(const mesh_cuda_kernel *k, int a, int *v, char **err) {
    CK(CU->cuFuncGetAttribute(v, a, k->func), "cuFuncGetAttribute");
    return true;
}

bool mesh_cuda_kernel_regs(const mesh_cuda_kernel *k, uint32_t *out, char **err) {
    int v = 0;
    if (!func_attr(k, CU_FUNC_ATTRIBUTE_NUM_REGS, &v, err)) return false;
    *out = (uint32_t)v;
    return true;
}

bool mesh_cuda_kernel_local_bytes(const mesh_cuda_kernel *k, uint32_t *out, char **err) {
    int v = 0;
    if (!func_attr(k, CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES, &v, err)) return false;
    *out = (uint32_t)v;
    return true;
}

bool mesh_cuda_kernel_set_shape(mesh_cuda_kernel *k, uint32_t threads, uint32_t smem, char **err) {
    int max = 0;
    if (!func_attr(k, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, &max, err)) return false;
    if (threads > (uint32_t)max) {
        uint32_t regs = 0;
        if (!mesh_cuda_kernel_regs(k, &regs, err)) return false;
        return mesh_errf(err, "regs: %u registers per thread allow %d threads per block; the node has %u", (unsigned)regs, max,
                         (unsigned)threads);
    }
    int stat = 0;
    if (!func_attr(k, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, &stat, err)) return false;
    if (smem + (uint32_t)stat > 48u << 10)
        CK(CU->cuFuncSetAttribute(k->func, CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES, (int)smem),
           "cuFuncSetAttribute(MAX_DYNAMIC_SHARED_SIZE_BYTES)");
    k->threads = threads;
    k->smem = smem;
    return true;
}

bool mesh_cuda_kernel_alloc_scratch(mesh_cuda_kernel *k, uint32_t bytes, char **err) {
    if (bytes == 0 || k->scratch != 0) return true;
    CUdeviceptr d = 0;
    char *what = mesh_fmt("cuMemAlloc(scratch %u bytes)", (unsigned)bytes);
    bool ok = mesh_cu_ck(CU->cuMemAlloc_v2(&d, bytes), what, err);
    free(what);
    if (!ok) return false;
    /* On a stream of its own, waited for: the program streams are non-blocking. */
    CUstream st = NULL;
    ok = mesh_cu_ck(CU->cuStreamCreate(&st, CU_STREAM_NON_BLOCKING), "cuStreamCreate", err);
    if (ok) {
        ok = mesh_cu_ck(CU->cuMemsetD8Async(d, 0, bytes, st), "cuMemsetD8Async(scratch)", err) &&
             mesh_cu_ck(CU->cuStreamSynchronize(st), "cuStreamSynchronize", err);
        CU->cuStreamDestroy_v2(st);
    }
    if (!ok) {
        CU->cuMemFree_v2(d);
        return false;
    }
    k->scratch = d;
    return true;
}

bool mesh_cuda_kernel_max_blocks_per_sm(const mesh_cuda_kernel *k, uint32_t *out, char **err) {
    int v = 0;
    CK(CU->cuOccupancyMaxActiveBlocksPerMultiprocessor(&v, k->func, (int)k->threads, k->smem),
       "cuOccupancyMaxActiveBlocksPerMultiprocessor");
    *out = (uint32_t)v;
    return true;
}

bool mesh_cuda_kernel_global_u32(const mesh_cuda_kernel *k, const char *name, uint32_t *out, char **err) {
    CUdeviceptr d = 0;
    size_t bytes = 0;
    char *what = mesh_fmt("cuModuleGetGlobal(%s)", name);
    bool ok = mesh_cu_ck(CU->cuModuleGetGlobal_v2(&d, &bytes, k->module, name), what, err);
    free(what);
    if (!ok) return false;
    if (bytes != 4) return mesh_errf(err, "global %s is %zu bytes, not 4", name, bytes);
    uint32_t v = 0;
    CK(CU->cuMemcpyDtoH_v2(&v, d, 4), "cuMemcpyDtoH");
    *out = v;
    return true;
}
