/* The simulated device. */
#include "mesh/cuda_fake.h"
#include "mesh/text.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static mesh_fake_cuda_cfg cfg;
static mesh_fake_cuda_stats st;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static char fail_call[64];
static CUresult fail_code;
static size_t stack_limit = 1024;

typedef struct CUmod_st {
    char entry[64];
    int regs, local, maxthreads, shared;
    uint32_t rt_abi;
    struct CUfunc_st *func;
} fmod;

struct CUfunc_st {
    fmod *mod;
    int max_dyn_smem;
};

struct CUstream_st {
    atomic_int busy;
};

static int dummy_ctx, dummy_green, dummy_desc;

static CUresult failing(const char *call) {
    pthread_mutex_lock(&mu);
    CUresult r = 0;
    if (fail_call[0] && strcmp(fail_call, call) == 0) {
        r = fail_code;
        fail_call[0] = 0;
    }
    pthread_mutex_unlock(&mu);
    return r;
}
#define FAILS(name)                       \
    do {                                  \
        CUresult r_ = failing(name);      \
        if (r_) return r_;                \
    } while (0)

static CUresult f_init(unsigned f) {
    (void)f;
    FAILS("cuInit");
    st.inits++;
    return 0;
}
static CUresult f_err_name(CUresult r, const char **name) {
    *name = mesh_cu_result_name(r);
    return *name ? 0 : 1;
}
static CUresult f_loading(int *mode) {
    *mode = cfg.loading_mode;
    return 0;
}
static CUresult f_dev_get(CUdevice *d, int o) {
    FAILS("cuDeviceGet");
    *d = o;
    return 0;
}
static CUresult f_dev_name(char *n, int len, CUdevice d) {
    (void)d;
    snprintf(n, (size_t)len, "%s", cfg.name);
    return 0;
}
static CUresult f_dev_attr(int *v, int a, CUdevice d) {
    (void)d;
    switch (a) {
    case CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK: *v = cfg.max_threads; break;
    case CU_DEVICE_ATTRIBUTE_MAX_REGISTERS_PER_BLOCK: *v = cfg.regs_per_block; break;
    case CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT: *v = cfg.sms; break;
    case CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR: *v = cfg.cc_major; break;
    case CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR: *v = cfg.cc_minor; break;
    case CU_DEVICE_ATTRIBUTE_HOST_NATIVE_ATOMIC_SUPPORTED: *v = cfg.host_atomics; break;
    case CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK_OPTIN: *v = cfg.max_shared_optin; break;
    case CU_DEVICE_ATTRIBUTE_MAX_BLOCKS_PER_MULTIPROCESSOR: *v = cfg.blocks_per_sm; break;
    default: return 1; /* CUDA_ERROR_INVALID_VALUE */
    }
    return 0;
}
static CUresult f_primary(CUcontext *c, CUdevice d) {
    (void)d;
    st.primary_ctx++;
    *c = (CUcontext)(void *)&dummy_ctx;
    return 0;
}
static CUresult f_set_current(CUcontext c) {
    (void)c;
    return 0;
}
static CUresult f_get_limit(size_t *v, int l) {
    if (l != CU_LIMIT_STACK_SIZE) return 1;
    *v = stack_limit;
    return 0;
}
static CUresult f_set_limit(int l, size_t v) {
    if (l != CU_LIMIT_STACK_SIZE) return 1;
    stack_limit = v;
    st.stack_limit = v;
    return 0;
}
static CUresult f_dev_resource(CUdevice d, CUdevResource *r, int type) {
    (void)d;
    if (type != CU_DEV_RESOURCE_TYPE_SM) return 1;
    r->type = type;
    r->u.sm.smCount = (unsigned)cfg.sms;
    return 0;
}
static CUresult f_split(CUdevResource *res, unsigned *n, const CUdevResource *in, CUdevResource *rem, unsigned flags, unsigned min) {
    (void)flags;
    if (min > in->u.sm.smCount || min % 2) {
        *n = 0;
        return 0;
    }
    res->type = CU_DEV_RESOURCE_TYPE_SM;
    res->u.sm.smCount = min;
    rem->type = CU_DEV_RESOURCE_TYPE_SM;
    rem->u.sm.smCount = in->u.sm.smCount - min;
    *n = 1;
    st.green_sms = (int)min;
    return 0;
}
static CUresult f_gen_desc(CUdevResourceDesc *d, CUdevResource *r, unsigned n) {
    (void)r, (void)n;
    *d = (CUdevResourceDesc)(void *)&dummy_desc;
    return 0;
}
static CUresult f_green_create(CUgreenCtx *g, CUdevResourceDesc d, CUdevice dev, unsigned f) {
    (void)d, (void)dev, (void)f;
    st.green_ctx++;
    *g = (CUgreenCtx)(void *)&dummy_green;
    return 0;
}
static CUresult f_from_green(CUcontext *c, CUgreenCtx g) {
    (void)g;
    *c = (CUcontext)(void *)&dummy_ctx;
    return 0;
}
static CUresult new_stream(CUstream *s) {
    FAILS("cuStreamCreate");
    struct CUstream_st *x = mesh_alloc(sizeof *x);
    atomic_init(&x->busy, 0);
    *s = x;
    pthread_mutex_lock(&mu);
    st.streams_created++;
    pthread_mutex_unlock(&mu);
    return 0;
}
static CUresult f_green_stream(CUstream *s, CUgreenCtx g, unsigned f, int p) {
    (void)g, (void)f, (void)p;
    return new_stream(s);
}
static CUresult f_stream_create(CUstream *s, unsigned f) {
    (void)f;
    return new_stream(s);
}
static CUresult f_stream_destroy(CUstream s) {
    pthread_mutex_lock(&mu);
    st.streams_destroyed++;
    pthread_mutex_unlock(&mu);
    /* A stream with a running kernel is leaked rather than freed under it. */
    if (atomic_load(&s->busy) == 0) free(s);
    return 0;
}
static CUresult f_stream_query(CUstream s) {
    FAILS("cuStreamQuery");
    return atomic_load(&s->busy) ? CUDA_ERROR_NOT_READY : 0;
}
static CUresult f_stream_sync(CUstream s) {
    while (atomic_load(&s->busy)) {
        struct timespec t = {0, 100000};
        nanosleep(&t, NULL);
    }
    return 0;
}
static CUresult f_host_register(void *p, size_t n, unsigned flags) {
    (void)p, (void)n;
    FAILS("cuMemHostRegister");
    if (!(flags & CU_MEMHOSTREGISTER_DEVICEMAP)) return 1;
    st.host_registered++;
    return 0;
}
static CUresult f_host_dev_ptr(CUdeviceptr *d, void *p, unsigned f) {
    (void)f;
    *d = (CUdeviceptr)(uintptr_t)p;
    return 0;
}
static int kv(const char *s, const char *key, int dflt) {
    const char *p = strstr(s, key);
    return p ? atoi(p + strlen(key)) : dflt;
}
static CUresult f_load_data(CUmodule *m, const void *image) {
    FAILS("cuModuleLoadData");
    const char *s = image;
    if (strncmp(s, "FAKECUBIN", 9) != 0) return 200; /* CUDA_ERROR_INVALID_IMAGE */
    fmod *x = mesh_alloc(sizeof *x);
    const char *e = strstr(s, "entry=");
    if (e) sscanf(e + 6, "%63s", x->entry);
    else strcpy(x->entry, "mesh_main");
    x->regs = kv(s, "regs=", 32);
    x->local = kv(s, "local=", 0);
    x->maxthreads = kv(s, "maxthreads=", 1024);
    x->shared = kv(s, "shared=", 0);
    x->rt_abi = (uint32_t)kv(s, "rt_abi=", 6);
    *m = x;
    pthread_mutex_lock(&mu);
    st.modules_loaded++;
    st.modules_live++;
    pthread_mutex_unlock(&mu);
    return 0;
}
static CUresult f_get_function(CUfunction *f, CUmodule m, const char *name) {
    if (strcmp(m->entry, name) != 0) return 500; /* CUDA_ERROR_NOT_FOUND */
    if (!m->func) {
        m->func = mesh_alloc(sizeof *m->func);
        m->func->mod = m;
    }
    *f = m->func;
    return 0;
}
static CUresult f_unload(CUmodule m) {
    pthread_mutex_lock(&mu);
    st.modules_unloaded++;
    st.modules_live--;
    pthread_mutex_unlock(&mu);
    free(m->func);
    free(m);
    return 0;
}
static CUresult f_get_global(CUdeviceptr *d, size_t *bytes, CUmodule m, const char *name) {
    if (strcmp(name, "MESH_RT_ABI") != 0) return 500;
    *d = (CUdeviceptr)(uintptr_t)&m->rt_abi;
    *bytes = 4;
    return 0;
}
static CUresult f_func_attr(int *v, int a, CUfunction f) {
    switch (a) {
    case CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK: *v = f->mod->maxthreads; break;
    case CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES: *v = f->mod->shared; break;
    case CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES: *v = f->mod->local; break;
    case CU_FUNC_ATTRIBUTE_NUM_REGS: *v = f->mod->regs; break;
    default: return 1;
    }
    return 0;
}
static CUresult f_func_set_attr(CUfunction f, int a, int v) {
    if (a != CU_FUNC_ATTRIBUTE_MAX_DYNAMIC_SHARED_SIZE_BYTES) return 1;
    if (v > cfg.max_shared_optin) return 1;
    f->max_dyn_smem = v;
    st.opted_in_smem = v;
    return 0;
}
static CUresult f_occupancy(int *blocks, CUfunction f, int threads, size_t smem) {
    (void)f, (void)threads, (void)smem;
    *blocks = cfg.blocks_per_sm_occupancy ? cfg.blocks_per_sm_occupancy : 16;
    return 0;
}
static CUresult f_alloc(CUdeviceptr *d, size_t n) {
    FAILS("cuMemAlloc");
    void *p = malloc(n);
    if (!p) return 2;
    memset(p, 0xa5, n); /* garbage: the memset must clear it */
    *d = (CUdeviceptr)(uintptr_t)p;
    st.allocs++;
    return 0;
}
static CUresult f_free(CUdeviceptr d) {
    free((void *)(uintptr_t)d);
    st.frees++;
    return 0;
}
static CUresult f_memset(CUdeviceptr d, unsigned char v, size_t n, CUstream s) {
    (void)s;
    memset((void *)(uintptr_t)d, v, n);
    st.memsets++;
    return 0;
}
static CUresult f_memcpy_dtoh(void *dst, CUdeviceptr src, size_t n) {
    memcpy(dst, (const void *)(uintptr_t)src, n);
    return 0;
}

typedef struct {
    CUstream s;
    char entry[64];
    uint64_t mailbox;
    uint32_t slot, nodes, threads;
} launch;

static void *run_launch(void *arg) {
    launch *l = arg;
    if (cfg.run) cfg.run(l->entry, l->mailbox, l->slot, l->nodes, l->threads, cfg.user);
    pthread_mutex_lock(&mu);
    st.running--;
    pthread_mutex_unlock(&mu);
    atomic_fetch_sub(&l->s->busy, 1);
    free(l);
    return NULL;
}

static CUresult f_launch(CUfunction f, unsigned gx, unsigned gy, unsigned gz, unsigned bx, unsigned by, unsigned bz, unsigned smem,
                         CUstream s, void **params, void **extra) {
    (void)gy, (void)gz, (void)by, (void)bz, (void)extra;
    FAILS("cuLaunchKernel");
    if ((int)bx > f->mod->maxthreads || smem + (unsigned)f->mod->shared > (unsigned)(f->max_dyn_smem > 48 * 1024 ? f->max_dyn_smem : 48 * 1024))
        return 1; /* CUDA_ERROR_INVALID_VALUE: the shape does not fit */
    launch *l = mesh_alloc(sizeof *l);
    l->s = s;
    strcpy(l->entry, f->mod->entry);
    memcpy(&l->mailbox, params[0], 8);
    memcpy(&l->slot, params[1], 4);
    l->nodes = gx;
    l->threads = bx;
    pthread_mutex_lock(&mu);
    st.launches++;
    st.running++;
    st.last_grid = gx;
    st.last_block = bx;
    st.last_smem = smem;
    st.last_mailbox = l->mailbox;
    st.last_slot = l->slot;
    pthread_mutex_unlock(&mu);
    atomic_fetch_add(&s->busy, 1);
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &at, run_launch, l);
    pthread_attr_destroy(&at);
    return 0;
}

static const mesh_cuda_fns fns = {
    f_init, f_err_name, f_loading, f_dev_get, f_dev_name, f_dev_attr, f_primary, f_set_current, f_get_limit, f_set_limit,
    f_dev_resource, f_split, f_gen_desc, f_green_create, f_from_green, f_green_stream, f_stream_create, f_stream_destroy,
    f_stream_query, f_stream_sync, f_host_register, f_host_dev_ptr, f_load_data, f_get_function, f_unload, f_get_global,
    f_func_attr, f_func_set_attr, f_occupancy, f_alloc, f_free, f_memset, f_memcpy_dtoh, f_launch,
};

void mesh_fake_cuda_install(const mesh_fake_cuda_cfg *c) {
    mesh_fake_cuda_cfg d = {"Fake GB10", 12, 1, 48, 24, 1, 1024, 65536, 101376, CU_MODULE_EAGER_LOADING, 0, NULL, NULL};
    if (c) {
        if (c->name) d.name = c->name;
        if (c->cc_major) d.cc_major = c->cc_major, d.cc_minor = c->cc_minor;
        if (c->sms) d.sms = c->sms;
        if (c->blocks_per_sm) d.blocks_per_sm = c->blocks_per_sm;
        if (c->max_threads) d.max_threads = c->max_threads;
        if (c->regs_per_block) d.regs_per_block = c->regs_per_block;
        if (c->max_shared_optin) d.max_shared_optin = c->max_shared_optin;
        if (c->loading_mode) d.loading_mode = c->loading_mode;
        d.host_atomics = c->host_atomics;
        d.blocks_per_sm_occupancy = c->blocks_per_sm_occupancy;
        d.run = c->run;
        d.user = c->user;
    }
    cfg = d;
    memset(&st, 0, sizeof st);
    mesh_cuda_set(&fns);
}

mesh_fake_cuda_stats mesh_fake_cuda_stats_now(void) {
    pthread_mutex_lock(&mu);
    mesh_fake_cuda_stats s = st;
    pthread_mutex_unlock(&mu);
    return s;
}

void mesh_fake_cuda_fail_next(const char *call, CUresult code) {
    pthread_mutex_lock(&mu);
    snprintf(fail_call, sizeof fail_call, "%s", call);
    fail_code = code;
    pthread_mutex_unlock(&mu);
}

bool mesh_fake_cuda_idle(unsigned ms) {
    for (unsigned i = 0; i < ms * 10; i++) {
        if (mesh_fake_cuda_stats_now().running == 0) return true;
        struct timespec t = {0, 100000};
        nanosleep(&t, NULL);
    }
    return mesh_fake_cuda_stats_now().running == 0;
}
