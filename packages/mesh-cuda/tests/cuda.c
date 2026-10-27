/* cuda.rs against a simulated device: eager loading, primary and green contexts,
 * limits and register caps, a module's whole life (shape, scratch, globals,
 * occupancy, launch, query, unload), and the refusals with their messages. */
#include "mesh/check.h"
#include "mesh/cuda.h"
#include "mesh/cuda_fake.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static atomic_int ran;

static void kernel(const char *entry, uint64_t mailbox, uint32_t slot, uint32_t nodes, uint32_t threads, void *user) {
    (void)user;
    CHECK_S(entry, "mesh_main");
    _Atomic uint32_t *w = (_Atomic uint32_t *)(uintptr_t)(mailbox + 0x100);
    atomic_store(w, slot * 1000 + nodes * 10 + threads / 32);
    struct timespec t = {0, 20000000};
    nanosleep(&t, NULL);
    atomic_fetch_add(&ran, 1);
}

int main(void) {
    char *err = NULL;
    /* force_eager refuses another setting. */
    setenv("CUDA_MODULE_LOADING", "LAZY", 1);
    CHECK(!mesh_cuda_force_eager(&err));
    CHECK_S(err, "CUDA_MODULE_LOADING=\"LAZY\": refusing to run; module loading must be EAGER (a lazy load blocks behind resident kernels)");
    unsetenv("CUDA_MODULE_LOADING");
    mesh_cuda_gpu g;
    CHECK(!mesh_cuda_gpu_open(0, &g, &err));
    CHECK_S(err, "CUDA_MODULE_LOADING is not EAGER: call cuda::force_eager() first");
    CHECK(mesh_cuda_force_eager(&err));
    CHECK_S(getenv("CUDA_MODULE_LOADING"), "EAGER");

    /* A driver that loads lazily is refused. */
    mesh_fake_cuda_install(&(mesh_fake_cuda_cfg){.loading_mode = CU_MODULE_LAZY_LOADING});
    CHECK(!mesh_cuda_gpu_open(0, &g, &err));
    CHECK_S(err, "the driver loads modules CU_MODULE_LAZY_LOADING; EAGER is required (CUDA_MODULE_LOADING must be EAGER before cuInit)");

    mesh_fake_cuda_install(&(mesh_fake_cuda_cfg){.host_atomics = 1, .run = kernel});
    CHECK(mesh_cuda_gpu_open(0, &g, &err));
    CHECK_S(g.name, "Fake GB10");
    CHECK(g.cc_major == 12 && g.cc_minor == 1 && g.sms_total == 48 && g.sms == 48 && !g.green && g.blocks_per_sm == 24);
    CHECK(g.host_atomics);
    CHECK(g.limits.max_threads == 1024 && g.limits.regs_per_block == 65536 && g.limits.max_shared == 101376);
    CHECK_I(mesh_fake_cuda_stats_now().primary_ctx, 1);
    mesh_cuda_gpu_free(&g);

    /* A green context of 8 SMs; an odd split is refused. */
    CHECK(mesh_cuda_gpu_open(8, &g, &err));
    CHECK(g.green && g.sms == 8 && g.sms_total == 48);
    mesh_cuda_stream s;
    CHECK(mesh_cuda_new_stream(&g, &s, &err));
    mesh_cuda_stream_destroy(&s);
    mesh_cuda_gpu_free(&g);
    CHECK(!mesh_cuda_gpu_open(7, &g, &err));
    CHECK_S(err, "green context: cannot split 7 SMs off 48");

    /* Register caps and the block limits. */
    mesh_cuda_limits l = {1024, 65536, 101376};
    CHECK_U(mesh_cuda_reg_cap(&l, 1), 0);
    CHECK_U(mesh_cuda_reg_cap(&l, 8), 0);
    CHECK_U(mesh_cuda_reg_cap(&l, 16), 128);
    CHECK_U(mesh_cuda_reg_cap(&l, 32), 64);
    mesh_cuda_limits small = {1024, 16384, 49152};
    CHECK_U(mesh_cuda_reg_cap(&small, 32), 16);
    CHECK(mesh_cuda_limits_check(&l, "p", 32, 0x200, &err));
    CHECK(!mesh_cuda_limits_check(&small, "p", 33, 0x200, &err));
    CHECK_S(err, "occupancy: p: warps 33 = 1056 threads per node; the device allows 1024 per block");
    CHECK(!mesh_cuda_limits_check(&small, "p", 1, 50000, &err));
    CHECK_S(err, "occupancy: p: 50000 bytes of shared memory per node (the runtime's 512 + shared_bytes); the device allows 49152 per block");

    /* A module's life. */
    CHECK(mesh_cuda_gpu_open(0, &g, &err));
    uint8_t *mailbox = calloc(1, 0x1000);
    uint64_t dev = 0;
    CHECK(mesh_cuda_host_register(&g, mailbox, 0x1000, &dev, &err));
    CHECK_U(dev, (uintptr_t)mailbox);
    mesh_cuda_kernel k;
    const uint8_t cubin[] = "FAKECUBIN entry=mesh_main regs=40 local=128 maxthreads=512 rt_abi=6";
    CHECK(!mesh_cuda_load_cubin(&g, cubin, "other", &k, &err));
    CHECK_S(err, "cuModuleGetFunction(other): CUDA_ERROR_NOT_FOUND");
    CHECK_I(mesh_fake_cuda_stats_now().modules_live, 0);
    CHECK(mesh_cuda_load_cubin(&g, cubin, "mesh_main", &k, &err));
    CHECK(k.threads == 32 && k.smem == 0x200 && k.scratch == 0);
    uint32_t v;
    CHECK(mesh_cuda_kernel_regs(&k, &v, &err) && v == 40);
    CHECK(mesh_cuda_kernel_local_bytes(&k, &v, &err) && v == 128);
    CHECK(mesh_cuda_kernel_global_u32(&k, "MESH_RT_ABI", &v, &err) && v == 6);
    CHECK(!mesh_cuda_kernel_set_shape(&k, 1024, 0x200, &err));
    CHECK_S(err, "regs: 40 registers per thread allow 512 threads per block; the node has 1024");
    CHECK(mesh_cuda_kernel_set_shape(&k, 256, 0x200 + 0x10000, &err));
    CHECK_I(mesh_fake_cuda_stats_now().opted_in_smem, 0x10200);
    CHECK(mesh_cuda_kernel_alloc_scratch(&k, 4096, &err));
    CHECK(k.scratch != 0);
    for (int i = 0; i < 4096; i++) CHECK_U(((uint8_t *)(uintptr_t)k.scratch)[i] | (i == 4095 ? 0 : 0), 0);
    CHECK(mesh_cuda_kernel_max_blocks_per_sm(&k, &v, &err) && v == 16);
    size_t stack;
    CHECK(mesh_cuda_set_stack_limit(&g, 4096, &err) && mesh_cuda_stack_limit(&g, &stack, &err) && stack == 4096);
    CHECK(mesh_cuda_new_stream(&g, &s, &err));
    uint64_t dmb = dev;
    uint32_t slot = 3;
    void *params[] = {&dmb, &slot};
    CHECK(mesh_cuda_launch_on(&g, &s, &k, 2, params, &err));
    bool done = true;
    CHECK(mesh_cuda_query(&g, &s, &done, &err) && !done);
    CHECK(mesh_fake_cuda_idle(2000));
    CHECK(mesh_cuda_query(&g, &s, &done, &err) && done);
    CHECK_U(*(uint32_t *)(mailbox + 0x100), 3 * 1000 + 2 * 10 + 8);
    mesh_fake_cuda_stats fs = mesh_fake_cuda_stats_now();
    CHECK(fs.last_grid == 2 && fs.last_block == 256 && fs.last_smem == 0x10200 && fs.last_slot == 3);
    CHECK(mesh_cuda_unload(&g, &k, &err));
    fs = mesh_fake_cuda_stats_now();
    CHECK(fs.modules_live == 0 && fs.frees == 1);
    mesh_cuda_stream_destroy(&s);
    /* A failed launch carries the driver's name for the error. */
    mesh_fake_cuda_fail_next("cuModuleLoadData", 200);
    CHECK(!mesh_cuda_load_cubin(&g, cubin, "mesh_main", &k, &err));
    CHECK_S(err, "cuModuleLoadData(cubin): CUDA_ERROR_INVALID_IMAGE");
    free(err);
    free(mailbox);
    mesh_cuda_gpu_free(&g);
    return mesh_check_done();
}
