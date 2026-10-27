/* kcheck's X and XG checks (mesh-kcheck pchecks.rs check_cross_arch) for the wasm images: train
 * run N tasks through the executor must equal the CPU reference (mesh-ref-cubic, in Z) bitwise
 * after every task, infer on the trained params must equal the reference cubic on the grid, and --
 * for the full run of 4096 tasks -- the digests of the samples, train's checkpoints, params and
 * losses and infer's grid must be the GPUs' own (GPU_DIGESTS: the same on dgx-00, being and the Mac
 * in the Rust's measurements). The full run is on by default in the release profile, or with
 * MESH_FULL=1; otherwise 256 tasks. */
#include "mesh/abi/control_block.h"
#include "mesh/abi/fnv.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/view_infer.h"
#include "mesh/abi/view_train.h"
#include "mesh/check.h"
#include "mesh/exec_wasm_testkit.h"
#include "mesh/ftz.h"
#include "mesh/lease.h"
#include "mesh/ref/cubic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TRAIN = "name = \"train\"\nabi = 6\nview_bytes = 0x1000\nview_abi = 1\nsnap_bytes = 0xd00\nnodes = 1\n"
                           "policy = \"continuous\"\nfanout = 1\narg = \"view_u32:0x1c\"\npeer = \"lockstep\"\n"
                           "counters = [\"steps\", \"timeouts\", \"inits\", \"\"]\nautostart = true\n"
                           "backends = [\"cuda\", \"metal\", \"wasm\"]\ntask_ms = 10000\n";
static const char *INFER = "name = \"infer\"\nabi = 6\nview_bytes = 0x1000\nview_abi = 1\nnodes = 1\npolicy = \"periodic\"\n"
                           "hz = 10\nreads = [\"train\"]\nbackends = [\"cuda\", \"metal\", \"wasm\"]\ntask_ms = 10000\n";

static const struct {
    const char *name;
    uint64_t want;
} GPU_DIGESTS[5] = {{"samples", 0x88ad9ee53d7f193dull},
                    {"train checkpoints", 0x10e3ab33652a552eull},
                    {"train params", 0x48104fa8535ef973ull},
                    {"train losses", 0xe511adc40fd1a525ull},
                    {"infer", 0x6612dd70d4a88dc4ull}};

static mesh_kit kit;

static float vf(uint32_t slot, uint32_t off) { return mesh_ftz_from_bits(mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, slot, off))); }
static uint32_t vu(uint32_t slot, uint32_t off) { return mesh_mailbox_get(kit.mb, mesh_kit_view(&kit, slot, off)); }

static void put_f32s(uint8_t *out, const float *v, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint32_t b = mesh_ftz_bits(v[i]);
        memcpy(out + 4 * i, &b, 4);
    }
}

int main(void) {
    char *train = mesh_kit_artifact("ajax-mesh-prog-train-wasm"), *infer = mesh_kit_artifact("ajax-mesh-prog-infer-wasm");
    CHECK(train && infer);
    if (!train || !infer) return mesh_check_done();
    char exe[512] = {0};
    bool full = getenv("MESH_FULL") != NULL;
    FILE *f = fopen("/proc/self/cmdline", "r");
    if (f) {
        if (fgets(exe, sizeof exe, f) && strstr(exe, "/release/")) full = true;
        fclose(f);
    }
    uint32_t tasks = full ? 4096 : 32;
    char *err = NULL;
    CHECK(mesh_kit_open(&kit, NULL, &err));
    if (!mesh_kit_load(&kit, 0, TRAIN, train, &err) || !mesh_kit_load(&kit, 1, INFER, infer, &err)) {
        fprintf(stderr, "load: %s\n", err);
        CHECK(false);
        return mesh_check_done();
    }
    uint32_t r, fault;
    CHECK(mesh_kit_task(&kit, 0, MESH_OP_INIT, 0, &r, &fault, 10000));
    float lr = vf(0, MESH_TRAIN_T_LR), inv = vf(0, MESH_TRAIN_T_INV_BATCH);
    uint32_t spt = vu(0, MESH_TRAIN_T_STEPS_PER_TASK);
    CHECK(lr == MESH_TRAIN_LR);
    CHECK_U(spt, MESH_TRAIN_STEPS_PER_TASK);
    float xs[MESH_TRAIN_SAMPLES], ys[MESH_TRAIN_SAMPLES];
    mesh_cubic_samples(xs, ys);
    float p[4] = {0, 0, 0, 0};
    uint8_t trace[16 * 16];
    size_t ntrace = 0;
    uint32_t bad = 0;
    uint64_t t0 = mesh_host_ns();
    for (uint32_t task = 0; task < tasks; task++) {
        uint32_t s = vu(0, MESH_TRAIN_T_NEXT_STEP);
        if (!mesh_kit_task(&kit, 0, MESH_OP_TASK, s, &r, &fault, 10000)) {
            bad++;
            break;
        }
        for (uint32_t st = s; st < s + spt; st++) {
            float sums[5];
            mesh_cubic_step_sums(p, xs, ys, st, sums);
            mesh_cubic_apply(p, sums, lr, inv);
        }
        float g[4];
        for (uint32_t k = 0; k < 4; k++) g[k] = vf(0, MESH_TRAIN_T_PARAMS + 4 * k);
        if (r != spt || memcmp(g, p, sizeof g) != 0) bad++;
        if (task % 256 == 255 && ntrace + 16 <= sizeof trace) {
            put_f32s(trace + ntrace, g, 4);
            ntrace += 16;
        }
    }
    double ms = (double)(mesh_host_ns() - t0) / 1e6;
    CHECK_U(bad, 0);
    uint8_t losses[256 * 4], pw[16], sw[2 * MESH_TRAIN_SAMPLES * 4];
    float l[256], fp[4], sv[2 * MESH_TRAIN_SAMPLES];
    for (uint32_t i = 0; i < 256; i++) l[i] = vf(0, MESH_TRAIN_T_LOSS + 4 * i);
    for (uint32_t k = 0; k < 4; k++) fp[k] = vf(0, MESH_TRAIN_T_PARAMS + 4 * k);
    for (uint32_t i = 0; i < 2 * MESH_TRAIN_SAMPLES; i++) sv[i] = vf(0, MESH_TRAIN_T_X + 4 * i);
    put_f32s(losses, l, 256);
    put_f32s(pw, fp, 4);
    put_f32s(sw, sv, 2 * MESH_TRAIN_SAMPLES);
    /* infer on the trained params (it reads train's view). */
    CHECK(mesh_kit_task(&kit, 1, MESH_OP_INIT, 0, &r, &fault, 10000));
    CHECK(mesh_kit_task(&kit, 1, MESH_OP_TASK, 0, &r, &fault, 10000));
    CHECK_U(r, MESH_INFER_POINTS);
    float grid[MESH_INFER_POINTS], yi[MESH_INFER_POINTS];
    mesh_cubic_grid(MESH_INFER_POINTS, grid);
    uint32_t infer_bad = 0;
    for (uint32_t k = 0; k < MESH_INFER_POINTS; k++) {
        yi[k] = vf(1, MESH_INFER_I_Y + 4 * k);
        float want = mesh_cubic(fp, grid[k]);
        if (mesh_ftz_bits(want) != mesh_ftz_bits(yi[k])) infer_bad++;
    }
    CHECK_U(infer_bad, 0);
    uint8_t iw[MESH_INFER_POINTS * 4];
    put_f32s(iw, yi, MESH_INFER_POINTS);
    uint64_t got[5] = {mesh_fnv1a64(sw, sizeof sw), mesh_fnv1a64(trace, ntrace), mesh_fnv1a64(pw, sizeof pw),
                       mesh_fnv1a64(losses, sizeof losses), mesh_fnv1a64(iw, sizeof iw)};
    fprintf(stderr, "X: train %u tasks x %u steps bitwise == CPU (%.0f ms), infer bitwise == CPU; digests:", tasks, spt, ms);
    for (int i = 0; i < 5; i++) fprintf(stderr, " %s %016llx", GPU_DIGESTS[i].name, (unsigned long long)got[i]);
    fprintf(stderr, "\n");
    /* The samples are the same whatever the length of the run. */
    CHECK_U(got[0], GPU_DIGESTS[0].want);
    if (full) {
        for (int i = 1; i < 5; i++) CHECK_U(got[i], GPU_DIGESTS[i].want);
        fprintf(stderr, "XG: the digests are the GPUs'\n");
    }
    /* infer's probe: train's view is readable, an undeclared one is not. */
    CHECK(mesh_kit_task(&kit, 1, MESH_INFER_OP_PROBE_READ, 0, &r, &fault, 10000));
    CHECK_U(r, 0x1000);
    mesh_kit_close(&kit);
    free(train);
    free(infer);
    free(err);
    return mesh_check_done();
}
