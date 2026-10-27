/* pathtrace-core's default_scene_is_valid and payload_digest_and_set (the set half), every
 * validation rule on a mutated default scene with the codes the Rust returns, and job scenes with a
 * golden digest from the Rust. */
#include "pathtrace/job_scene.h"

#include "pathtrace/scene.h"
#include "pathtrace/view.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define N PT_DEFAULT_SCENE_WORDS

int main(void) {
    static uint32_t b[N];
    CHECK(pt_validate(pt_default_scene, N) == 0);
    CHECK(pt_tiles(pt_default_scene) == 300);
    memcpy(b, pt_default_scene, sizeof b);
    b[PT_H_WIDTH] = 600;
    CHECK(pt_validate(b, N) == 4);

    /* Codes, as the Rust returns them (gold pathtrace "validate"). */
    CHECK(pt_validate(pt_default_scene, 20) == 1);
    const struct {
        size_t word;
        uint32_t value, code;
    } muts[15] = {{PT_H_MAGIC, 1, 2},   {PT_H_VERSION, 2, 2},  {PT_H_WORDS, 2000, 3},  {PT_H_WORDS, 551, 7},
                  {PT_H_WIDTH, 600, 4}, {PT_H_HEIGHT, 0, 4},   {PT_H_SPP, 0, 5},       {PT_H_PASSES, 5000, 5},
                  {PT_H_DEPTH, 17, 5},  {PT_H_NMAT, 0, 6},     {PT_H_NQUAD, 65, 6},    {PT_H_NTRI, 19, 7},
                  {PT_HEADER_WORDS, 9, 8}, {PT_H_LIGHT, 6, 10}, {PT_H_LIGHT, 0xffffffffu, 0}};
    for (int i = 0; i < 15; i++) {
        memcpy(b, pt_default_scene, sizeof b);
        b[muts[i].word] = muts[i].value;
        if (pt_validate(b, N) != muts[i].code) {
            fprintf(stderr, "word %zu := %u: code %u, want %u\n", muts[i].word, muts[i].value, pt_validate(b, N),
                    muts[i].code);
            failures++;
        }
    }
    pt_layout l = pt_layout_of(pt_default_scene);
    const size_t mat_words[3] = {l.quad + 15, l.sph + 5, l.tri + 12};
    for (int i = 0; i < 3; i++) {
        memcpy(b, pt_default_scene, sizeof b);
        b[mat_words[i]] = 99;
        CHECK(pt_validate(b, N) == 9);
    }

    /* OP_SET's args set exactly the settable words. */
    memcpy(b, pt_default_scene, sizeof b);
    CHECK(pt_set_param(b, N, pt_set_arg(PT_H_WIDTH, 1280)) && pt_set_param(b, N, pt_set_arg(PT_H_HEIGHT, 720)) &&
          pt_set_param(b, N, pt_set_arg(PT_H_SPP, 32)));
    CHECK(!pt_set_param(b, N, pt_set_arg(PT_H_WORDS, 7)) && !pt_set_param(b, N, pt_set_arg(PT_H_MAGIC, 1)));
    CHECK(b[PT_H_WIDTH] == 1280 && b[PT_H_HEIGHT] == 720 && b[PT_H_SPP] == 32 && pt_tiles(b) == 1200);
    CHECK(pt_validate(b, N) == 4);
    CHECK(pt_validate_in(b, N, PT_FLEET_MAX_TILES) == 0);

    /* Job scenes (gold pathtrace "job_scene"). */
    const pt_setup_task setup[4] = {{PT_OP_SET, pt_set_arg(PT_H_WIDTH, 2048)},
                                    {PT_OP_SET, pt_set_arg(PT_H_HEIGHT, 1536)},
                                    {PT_OP_DEFAULT, 0},
                                    {PT_OP_SET, pt_set_arg(PT_H_SPP, 3)}};
    CHECK(pt_job_scene(setup, 4, b) == -1);
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < N; i++) h = (h ^ b[i]) * 0x100000001b3ull;
    CHECK(h == 0x3e59d8a64c44a945ull);
    const pt_setup_task bad1[1] = {{PT_OP_SET, pt_set_arg(PT_H_WORDS, 1)}};
    const pt_setup_task bad2[2] = {{PT_OP_DEFAULT, 0}, {9, 0}};
    CHECK(pt_job_scene(bad1, 1, b) == 0);
    CHECK(pt_job_scene(bad2, 2, b) == 1);
    return failures != 0;
}
