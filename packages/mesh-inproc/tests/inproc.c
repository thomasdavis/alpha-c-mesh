/* inproc.rs as kcheck uses it, on the fake CUDA device (blink mode) with a
 * fake nvJitLink: a catalog dir loaded and checked, installed in a slot,
 * launched, tickets one by one and in a batch, the input queue, a request
 * through the window, a stop and a relaunch, the heap and reads masks. */
#include "mesh/check.h"
#include "mesh/cuda_fake.h"
#include "mesh/inproc.h"
#include "mesh/mps.h"
#include "mesh/worker_node.h"

#include "mesh/abi/ops.h"
#include "mesh/abi/view_heap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void p_init(const mesh_worker_ctx *c, void *u) {
    (void)u;
    mesh_ctx_set_u32(c, 0, 0x4f4c4548);
}
static uint32_t p_task(const mesh_worker_ctx *c, uint32_t op, uint32_t arg, void *u) {
    (void)u;
    if (op == MESH_OP_TASK) return arg * 3 + 7;
    if (op == MESH_OP_REQ) {
        mesh_view rq = mesh_ctx_request(c), rs = mesh_ctx_response(c);
        uint32_t n = mesh_view_len(rq);
        for (uint32_t i = 0; i < n; i++) mesh_view_set_u8(rs, i, mesh_view_u8(rq, n - 1 - i));
        mesh_ctx_respond(c, n);
        return n;
    }
    if (op == 7) {
        uint32_t sum = 0;
        uint8_t b;
        while (mesh_ctx_input_pop(c, &b)) sum += b;
        return sum;
    }
    return MESH_R_BAD_OP;
}
static const mesh_worker_program prog = {p_init, p_task, NULL};
static void kernel(const char *e, uint64_t mb, uint32_t slot, uint32_t nodes, uint32_t threads, void *u) {
    (void)e, (void)nodes, (void)threads, (void)u;
    mesh_worker_run_nodes(mesh_mailbox_new((uint8_t *)(uintptr_t)mb, 0), slot, &prog, NULL);
}

static int j_create(void **h, uint32_t n, const char *const *o) {
    (void)n, (void)o;
    *h = (void *)1;
    return 0;
}
static int j_destroy(void **h) {
    *h = NULL;
    return 0;
}
static int j_add(void *h, int k, const void *d, size_t n, const char *name) {
    (void)h, (void)k, (void)d, (void)n, (void)name;
    return 0;
}
static int j_complete(void *h) {
    (void)h;
    return 0;
}
static const char cubin[] = "FAKECUBIN entry=mesh_main regs=32 rt_abi=6";
static int j_size(void *h, size_t *n) {
    (void)h;
    *n = sizeof cubin;
    return 0;
}
static int j_cubin(void *h, void *out) {
    (void)h;
    memcpy(out, cubin, sizeof cubin);
    return 0;
}
static int j_none(void *h, size_t *n) {
    (void)h;
    *n = 0;
    return 0;
}
static int j_log(void *h, void *o) {
    (void)h, (void)o;
    return 0;
}
static const mesh_nvjitlink_fns jit_fns = {j_create, j_destroy, j_add, j_complete, j_size, j_cubin, j_none, j_log};

static void write_file(const char *p, const char *s) {
    FILE *f = fopen(p, "w");
    fputs(s, f);
    fclose(f);
}

int main(void) {
    setenv("CUDA_MODULE_LOADING", "EAGER", 1);
    mesh_fake_cuda_install(&(mesh_fake_cuda_cfg){.run = kernel});
    char *err = NULL;
    mesh_shm shm;
    CHECK(mesh_shm_new(&shm, &err));
    mesh_nvjitlink jit;
    mesh_nvjitlink_from(&jit_fns, &jit);
    mesh_backend *b;
    if (!CHECK(mesh_cuda_backend_open(MESH_ISOLATION_MODE_BLINK, 0, NULL, 0, &shm, &jit, &b, &err))) {
        fprintf(stderr, "%s\n", err);
        return mesh_check_done();
    }
    mesh_dev d;
    mesh_dev_from(b, &shm, &d);
    CHECK(mesh_dev_cuda(&d) && !mesh_dev_wasm(&d));
    CHECK_S(mesh_dev_code(&d), "cubin");
    CHECK_U(mesh_dev_sms_total(&d), 48);
    mesh_mb2 mb;
    CHECK(mesh_mb2_alloc(&d, &mb, &err));
    CHECK_U(mesh_mb2_r32(mb, 0), 0x4853454d);

    char root[] = "/tmp/mesh-inproc-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char *dir = mesh_fmt("%s/echo", root), *toml = mesh_fmt("%s/program.toml", dir), *ptx = mesh_fmt("%s/echo.ptx", dir);
    mkdir(dir, 0755);
    write_file(toml, "name = \"echo\"\nabi = 6\nview_bytes = 0x1000\nnodes = \"fit\"\ninput = 64\nrequest = \"0x100:0x40,0x200:0x40\"\nreads = [\"other\"]\n");
    write_file(ptx, ".visible .entry mesh_main() { ret; }\n");
    mesh_image2 im;
    if (!CHECK(mesh_inproc_load(&d, dir, &im, &err))) {
        fprintf(stderr, "%s\n", err);
        return mesh_check_done();
    }
    CHECK(im.rt_abi == 6 && im.regs == 32 && im.cubin_bytes == sizeof cubin);
    /* nodes = "fit": 3/4 of 16 per SM x 48 SMs, at most PROG_MAX_NODES. */
    CHECK_U(im.manifest.nodes, 32);
    uint32_t mask;
    mesh_slot_name slots[] = {{"other", 9}, {"echo", 2}};
    CHECK(mesh_reads_mask(&im.manifest, slots, 2, &mask, &err) && mask == 1u << 9);
    CHECK(!mesh_reads_mask(&im.manifest, slots + 1, 1, &mask, &err));
    CHECK_S(err, "echo: reads \"other\", which is not loaded");
    mesh_heap h = mesh_heap_new();
    uint32_t v1, v2;
    CHECK(mesh_heap_alloc(&h, 0x1000, &v1, &err) && v1 == MESH_VIEW_HEAP);
    CHECK(mesh_heap_alloc(&h, 0x1001, &v2, &err) && v2 == MESH_VIEW_HEAP + 0x1000);
    CHECK_U(h.next, MESH_VIEW_HEAP + 0x3000);
    mesh_prog p;
    CHECK(mesh_prog_install(&d, mb, 2, &im, v1, mask, &p, &err));
    double ms;
    if (!CHECK(mesh_prog_launch(&d, &p, &ms, &err))) fprintf(stderr, "%s\n", err);
    uint32_t r;
    CHECK(mesh_prog_run(&p, MESH_OP_INIT, 0, &r, &err) && r == MESH_R_NONE);
    CHECK_U(mesh_mb2_r32(mb, v1), 0x4f4c4548);
    /* A batch is at most a ring of results (64): the Rust's batch too. */
    uint32_t res[60];
    if (!CHECK(mesh_prog_batch(&p, MESH_OP_TASK, 60, res, &err))) fprintf(stderr, "batch: %s\n", err);
    bool all = true;
    for (uint32_t a = 0; a < 60; a++) all = all && res[a] == a * 3 + 7;
    CHECK(all);
    CHECK(mesh_prog_input_push(&p, (const uint8_t *)"\x01\x02\x03", 3, &err));
    CHECK(mesh_prog_run(&p, 7, 0, &r, &err) && r == 6);
    uint8_t *resp;
    size_t rn;
    CHECK(mesh_prog_request(&p, (const uint8_t *)"hello", 5, &resp, &rn, &err));
    CHECK(rn == 5 && memcmp(resp, "olleh", 5) == 0);
    free(resp);
    /* Stop and relaunch: tickets continue from the tail. */
    CHECK(mesh_prog_stop(&d, &p, &ms, &err));
    CHECK(mesh_prog_launch(&d, &p, &ms, &err));
    CHECK(mesh_prog_run(&p, MESH_OP_TASK, 1000, &r, &err) && r == 3007);
    CHECK(mesh_prog_stop(&d, &p, &ms, &err));
    mesh_prog_free(&d, &p);
    CHECK_I(mesh_fake_cuda_stats_now().modules_live, 0);
    mesh_dev_close(&d);
    unlink(toml);
    unlink(ptx);
    rmdir(dir);
    rmdir(root);
    free(dir);
    free(toml);
    free(ptx);
    free(err);
    return mesh_check_done();
}
