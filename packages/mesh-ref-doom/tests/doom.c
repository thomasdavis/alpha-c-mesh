/* A golden digest from the Rust (mesh-ref lib.rs): the map; 2000 keys from the start pose (each pose,
 * and every column's ray and height every 50 keys); 5000 odd rays (random spots, any directions
 * including NaN and subnormal ones, short step limits), heights and walkable tests. */
#include "mesh/ref/doom.h"

#include "mesh/abi/view_doom.h"
#include "mesh/ftz.h"

#include <stdio.h>

static uint64_t fh(uint64_t h, uint64_t w) { return (h ^ w) * 0x100000001b3ull; }

static uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

static uint32_t gen(uint32_t m, uint32_t i) {
    uint32_t h = mix((i * 0x9e3779b9u) ^ (m * 0x85ebca6bu));
    uint32_t h2 = mix(h ^ 0x27d4eb2fu);
    if (m == 0) return h;
    if (m == 1) return (h & 0x80000000u) | ((h2 % 41) << 23) | (h & 0x007fffffu);
    return (h & 0x80000000u) | ((50 + h2 % 31) << 23) | (h & 0x007fffffu);
}

int main(void) {
    uint64_t h = 0xcbf29ce484222325ull;
    uint32_t map[256], side;
    float d;
    mesh_doom_map(map);
    for (int c = 0; c < 256; c++) h = fh(h, map[c]);
    static const char keys[] = "wsadqe";
    mesh_doom_pose pose = mesh_doom_pose_start();
    for (uint32_t i = 0; i < 2000; i++) {
        uint8_t key = mix(i) % 7 == 6 ? 'x' : (uint8_t)keys[(mix(i) % 7) % 6];
        mesh_doom_pose_apply(&pose, key);
        for (int k = 0; k < 6; k++) h = fh(h, mesh_ftz_bits(pose.p[k]));
        if (i % 50 == 0)
            for (uint32_t c = 0; c < MESH_DOOM_WIDTH; c++) {
                uint32_t k = mesh_doom_cast(map, pose.p[0], pose.p[1], pose.p[2], pose.p[3], pose.p[4], pose.p[5], c,
                                            MESH_DOOM_MAX_STEPS, &side, &d);
                h = fh(fh(fh(h, k), side), mesh_ftz_bits(d));
                h = fh(h, mesh_ftz_bits(mesh_doom_height(k, d)));
            }
    }
    for (uint32_t i = 0; i < 5000; i++) {
        float r[8];
        for (uint32_t j = 0; j < 8; j++) r[j] = mesh_ftz_from_bits(gen(j % 3, i * 8 + j));
        float px = (float)(mix(i) % 1600) / 100.0f, py = (float)(mix(i ^ 77) % 1600) / 100.0f;
        uint32_t k = mesh_doom_cast(map, px, py, r[2], r[3], r[4], r[5], mix(i) % 130, mix(i) % 70, &side, &d);
        h = fh(fh(fh(h, k), side), mesh_ftz_bits(d));
        h = fh(h, mesh_ftz_bits(mesh_doom_height(k, mesh_ftz_from_bits(gen(i % 3, i)))));
        h = fh(h, (uint64_t)mesh_doom_walkable(r[6], r[7]) + 2 * (uint64_t)mesh_doom_walkable(px, py));
    }
    if (h != 0xe1672eb4fa54e885ull) {
        fprintf(stderr, "doom %#018llx, want 0xe1672eb4fa54e885\n", (unsigned long long)h);
        return 1;
    }
    return 0;
}
