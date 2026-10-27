/* @ajax/cuda-driver: the CUDA driver API calls meshd (and kcheck, and the MPS
 * workers) make, as a table of functions loaded at runtime from libcuda.so.1.
 * Building needs no CUDA headers or toolkit; running needs the driver.  The
 * types and constants are the CUDA 13.0 ABI, as alpha-rust-mesh's cudarc
 * 0.19 bindings (feature cuda-13000) declare them.
 *
 * Tests swap the table (mesh_cuda_set) for a fake driver, so every call
 * sequence above it is testable without a GPU. */
#ifndef MESH_CUDA_DRIVER_H
#define MESH_CUDA_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int CUresult;
typedef int CUdevice;
typedef unsigned long long CUdeviceptr;
typedef struct CUctx_st *CUcontext;
typedef struct CUmod_st *CUmodule;
typedef struct CUfunc_st *CUfunction;
typedef struct CUstream_st *CUstream;
typedef struct CUgreenCtx_st *CUgreenCtx;
typedef struct CUdevResourceDesc_st *CUdevResourceDesc;

/* CUdevResource (CUDA 13.0: type, 92 bytes of padding, a 48-byte union), with
 * spare room after it in case a newer driver writes a longer struct. */
typedef struct {
    int type;
    unsigned char internal_padding[92];
    union {
        struct {
            unsigned int smCount;
            unsigned int minSmPartitionSize;
            unsigned int smCoscheduledAlignment;
        } sm;
        unsigned char oversize[48];
    } u;
    unsigned char spare[64];
} CUdevResource;

#define CUDA_SUCCESS 0
#define CUDA_ERROR_NOT_READY 600
/* CUdevice_attribute */
#define CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK 1
#define CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK 12
#define CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT 16
#define CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR 75
#define CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR 76
#define CU_DEVICE_ATTRIBUTE_HOST_NATIVE_ATOMIC_SUPPORTED 86
#define CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN 97
#define CU_DEVICE_ATTRIBUTE_MAX_BLOCKS_PER_MULTIPROCESSOR 106
/* CUfunction_attribute */
#define CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK 0
#define CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES 1
#define CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES 3
#define CU_FUNC_ATTRIBUTE_NUM_REGS 4
#define CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES 8
/* CUlimit, stream flags, host-register flags, module loading modes */
#define CU_LIMIT_STACK_SIZE 0
#define CU_STREAM_NON_BLOCKING 1u
#define CU_MEMHOSTREGISTER_PORTABLE 1u
#define CU_MEMHOSTREGISTER_DEVICEMAP 2u
#define CU_MODULE_EAGER_LOADING 1
#define CU_MODULE_LAZY_LOADING 2
/* green contexts */
#define CU_DEV_RESOURCE_TYPE_SM 1
#define CU_DEV_SM_RESOURCE_SPLIT_IGNORE_SM_COSCHEDULING 1u
#define CU_GREEN_CTX_DEFAULT_STREAM 1u

/* The calls, by their libcuda symbol names (the _v2 ones are what the CUDA
 * 13 headers' macros resolve to). */
typedef struct {
    CUresult (*cuInit)(unsigned int flags);
    CUresult (*cuGetErrorName)(CUresult r, const char **name);
    CUresult (*cuModuleGetLoadingMode)(int *mode);
    CUresult (*cuDeviceGet)(CUdevice *dev, int ordinal);
    CUresult (*cuDeviceGetName)(char *name, int len, CUdevice dev);
    CUresult (*cuDeviceGetAttribute)(int *v, int attrib, CUdevice dev);
    CUresult (*cuDevicePrimaryCtxRetain)(CUcontext *ctx, CUdevice dev);
    CUresult (*cuCtxSetCurrent)(CUcontext ctx);
    CUresult (*cuCtxGetLimit)(size_t *v, int limit);
    CUresult (*cuCtxSetLimit)(int limit, size_t v);
    CUresult (*cuDeviceGetDevResource)(CUdevice dev, CUdevResource *res, int type);
    CUresult (*cuDevSmResourceSplitByCount)(CUdevResource *result, unsigned int *nb_groups, const CUdevResource *input,
                                            CUdevResource *remaining, unsigned int flags, unsigned int min_count);
    CUresult (*cuDevResourceGenerateDesc)(CUdevResourceDesc *desc, CUdevResource *resources, unsigned int n);
    CUresult (*cuGreenCtxCreate)(CUgreenCtx *g, CUdevResourceDesc desc, CUdevice dev, unsigned int flags);
    CUresult (*cuCtxFromGreenCtx)(CUcontext *ctx, CUgreenCtx g);
    CUresult (*cuGreenCtxStreamCreate)(CUstream *s, CUgreenCtx g, unsigned int flags, int priority);
    CUresult (*cuStreamCreate)(CUstream *s, unsigned int flags);
    CUresult (*cuStreamDestroy_v2)(CUstream s);
    CUresult (*cuStreamQuery)(CUstream s);
    CUresult (*cuStreamSynchronize)(CUstream s);
    CUresult (*cuMemHostRegister_v2)(void *p, size_t bytes, unsigned int flags);
    CUresult (*cuMemHostGetDevicePointer_v2)(CUdeviceptr *d, void *p, unsigned int flags);
    CUresult (*cuModuleLoadData)(CUmodule *m, const void *image);
    CUresult (*cuModuleGetFunction)(CUfunction *f, CUmodule m, const char *name);
    CUresult (*cuModuleUnload)(CUmodule m);
    CUresult (*cuModuleGetGlobal_v2)(CUdeviceptr *d, size_t *bytes, CUmodule m, const char *name);
    CUresult (*cuFuncGetAttribute)(int *v, int attrib, CUfunction f);
    CUresult (*cuFuncSetAttribute)(CUfunction f, int attrib, int v);
    CUresult (*cuOccupancyMaxActiveBlocksPerMultiprocessor)(int *blocks, CUfunction f, int threads, size_t smem);
    CUresult (*cuMemAlloc_v2)(CUdeviceptr *d, size_t bytes);
    CUresult (*cuMemFree_v2)(CUdeviceptr d);
    CUresult (*cuMemsetD8Async)(CUdeviceptr d, unsigned char v, size_t n, CUstream s);
    CUresult (*cuMemcpyDtoH_v2)(void *dst, CUdeviceptr src, size_t n);
    CUresult (*cuLaunchKernel)(CUfunction f, unsigned int gx, unsigned int gy, unsigned int gz, unsigned int bx, unsigned int by,
                               unsigned int bz, unsigned int smem, CUstream s, void **params, void **extra);
} mesh_cuda_fns;

/* The table in use: the loaded driver, or the one a test set; NULL before either. */
const mesh_cuda_fns *mesh_cuda(void);
/* dlopen("libcuda.so.1") and every symbol above (once; later calls return the
 * first result).  False with *err ("cuda: cannot open libcuda.so.1: ..." or
 * "cuda: libcuda.so.1 has no cuX"). */
bool mesh_cuda_load(char **err);
/* Uses fns instead of the driver (tests); NULL: back to unloaded. */
void mesh_cuda_set(const mesh_cuda_fns *fns);
/* The table in use was set by mesh_cuda_set (a fake driver: no libcuda is mapped). */
bool mesh_cuda_injected(void);

/* The variant name of a CUresult ("CUDA_ERROR_NOT_READY"), as Rust's {:?}; NULL if unknown. */
const char *mesh_cu_result_name(CUresult r);
/* Rust's `{r:?}` of a CUresult: its name, or "cudaError_enum(N)" for an unknown code (malloc'd). */
char *mesh_cu_result_debug(CUresult r);
/* cuda.rs's ck: true on CUDA_SUCCESS; else *err = "<what>: <cuGetErrorName>" (or the Debug name). */
bool mesh_cu_ck(CUresult r, const char *what, char **err);
/* mps.rs's ck: "<what>: <Debug name>" (no cuGetErrorName). */
bool mesh_cu_ck_debug(CUresult r, const char *what, char **err);

typedef struct {
    CUresult code;
    const char *name;
} mesh_cu_name;

#endif
