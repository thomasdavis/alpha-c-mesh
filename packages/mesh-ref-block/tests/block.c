/* mesh-ref block.rs's test, then a golden digest from the Rust: every reduction and scan (f32 sum
 * and max, u32 and u64 sums; inclusive and exclusive) for 20 seeds at 32..1024 threads. */
#include "mesh/ref/block.h"

#include "mesh/ftz.h"

#include <stdio.h>

static int failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint64_t fnv(uint64_t h, uint64_t w) { return (h ^ w) * 0x100000001b3ull; }

static void rust_trees_and_identities(void) {
    /* The tree over 1024 leaves, whatever the warps: 4 warps == 32 warps with the rest -0.0. */
    float v[128], w[1024];
    uint32_t u[256];
    uint64_t dummy;
    float fd;
    for (uint32_t t = 0; t < 128; t++) mesh_block_test_values(7, t, &v[t], &u[0], &dummy);
    for (int t = 0; t < 1024; t++) w[t] = t < 128 ? v[t] : mesh_block_sum_f32_id();
    CHECK(mesh_ftz_bits(mesh_block_reduce_f32(v, 128, mesh_block_sum_f32_id(), mesh_block_add_f32)) ==
          mesh_ftz_bits(mesh_block_reduce_f32(w, 1024, mesh_block_sum_f32_id(), mesh_block_add_f32)));
    /* u32 sums: any order; scans are prefix sums. */
    for (uint32_t t = 0; t < 256; t++) mesh_block_test_values(9, t, &fd, &u[t], &dummy);
    uint32_t seq = 0;
    for (int t = 0; t < 256; t++) seq += u[t];
    CHECK(mesh_block_reduce_u32(u, 256, 0, mesh_block_add_u32) == seq);
    uint32_t inc[256], exc[256], acc = 0;
    CHECK(mesh_block_scan_u32(u, 256, 0, mesh_block_add_u32, true, inc));
    CHECK(mesh_block_scan_u32(u, 256, 0, mesh_block_add_u32, false, exc));
    for (int t = 0; t < 256; t++) {
        CHECK(exc[t] == acc);
        acc += u[t];
        CHECK(inc[t] == acc);
    }
    /* max: total order, one of the inputs. */
    CHECK(mesh_ftz_bits(mesh_block_max_f32(-0.0f, 0.0f)) == 0);
    CHECK(mesh_ftz_bits(mesh_block_max_f32(0.0f, -0.0f)) == 0);
    float m[32];
    const float pat[4] = {1.0f, -2.0f, 3.5f, mesh_ftz_from_bits(0xff800000u)};
    for (int i = 0; i < 32; i++) m[i] = pat[i % 4];
    CHECK(mesh_block_reduce_f32(m, 32, mesh_block_max_f32_id(), mesh_block_max_f32) == 3.5f);
    /* Invalid sizes. */
    CHECK(mesh_block_reduce_u32(u, 33, 7, mesh_block_add_u32) == 7);
    CHECK(!mesh_block_scan_u32(u, 0, 0, mesh_block_add_u32, true, inc));
}

static void rust_golden(void) {
    static const size_t ns[6] = {32, 64, 128, 256, 544, 1024};
    static float f[1024], fo[1024];
    static uint32_t u[1024], uo[1024];
    static uint64_t w[1024], wo[1024];
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t seed = 0; seed < 20; seed++) {
        for (int ni = 0; ni < 6; ni++) {
            size_t n = ns[ni];
            for (uint32_t t = 0; t < n; t++) mesh_block_test_values(seed, t, &f[t], &u[t], &w[t]);
            h = fnv(h, mesh_ftz_bits(mesh_block_reduce_f32(f, n, mesh_block_sum_f32_id(), mesh_block_add_f32)));
            h = fnv(h, mesh_ftz_bits(mesh_block_reduce_f32(f, n, mesh_block_max_f32_id(), mesh_block_max_f32)));
            h = fnv(h, mesh_block_reduce_u32(u, n, 0, mesh_block_add_u32));
            h = fnv(h, mesh_block_reduce_u64(w, n, 0, mesh_block_add_u64));
            for (int inc = 1; inc >= 0; inc--) {
                mesh_block_scan_f32(f, n, mesh_block_sum_f32_id(), mesh_block_add_f32, inc, fo);
                for (size_t i = 0; i < n; i++) h = fnv(h, mesh_ftz_bits(fo[i]));
                mesh_block_scan_f32(f, n, mesh_block_max_f32_id(), mesh_block_max_f32, inc, fo);
                for (size_t i = 0; i < n; i++) h = fnv(h, mesh_ftz_bits(fo[i]));
                mesh_block_scan_u32(u, n, 0, mesh_block_add_u32, inc, uo);
                for (size_t i = 0; i < n; i++) h = fnv(h, uo[i]);
                mesh_block_scan_u64(w, n, 0, mesh_block_add_u64, inc, wo);
                for (size_t i = 0; i < n; i++) h = fnv(h, wo[i]);
            }
        }
    }
    if (h != 0xb142436d933cba7eull) {
        fprintf(stderr, "block digest %#018llx, want 0xb142436d933cba7e\n", (unsigned long long)h);
        failures++;
    }
}

int main(void) {
    rust_trees_and_identities();
    rust_golden();
    return failures != 0;
}
