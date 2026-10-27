/* assemble.rs's tests: the ordered fold equals the CPU reference bit for bit, whatever order the blobs
 * come in and however often, each (tile, pass) folded once, a wrong payload refused; a tone-map swap;
 * the default scene's shape and job_scene's refusals. */
#include "mesh/assemble.h"
#include "mesh/assemble_bmp.h"
#include "mesh/check.h"
#include "mesh/ftz.h"
#include "mesh/ref/pathtrace.h"
#include "mesh/strbuf.h"
#include "pathtrace/digest.h"
#include "pathtrace/scene.h"
#include "pathtrace/view.h"
#include "pathtrace/z.h"

#include <stdlib.h>
#include <string.h>

#define PX PT_TILE_PIX

/* A small scene's tile pass as the programs compute it (Z: the wasm image's code). */
static uint64_t payload(const uint32_t *sc, uint32_t t, uint32_t p, uint8_t *out) {
    pt_v *part = mxalloc(PX * sizeof *part);
    uint64_t d = pt_tile_pass_z(sc, t, p, part);
    for (size_t i = 0; i < PX; i++) {
        uint32_t w[3] = {mesh_ftz_bits(part[i].x), mesh_ftz_bits(part[i].y), mesh_ftz_bits(part[i].z)};
        for (int k = 0; k < 3; k++)
            for (int b = 0; b < 4; b++) out[12 * i + 4 * (size_t)k + (size_t)b] = (uint8_t)(w[k] >> 8 * b);
    }
    free(part);
    return d;
}

static const mj_setup SMALL[] = {{5, 0}, {7, 0}, {7, 0}, {7, 0}, {7, 0}};

static void small(mj_setup s[5]) {
    memcpy(s, SMALL, sizeof SMALL);
    s[1].arg = pt_set_arg(PT_H_WIDTH, 96);
    s[2].arg = pt_set_arg(PT_H_HEIGHT, 48);
    s[3].arg = pt_set_arg(PT_H_PASSES, 5);
    s[4].arg = pt_set_arg(PT_H_SPP, 2);
}

static void ordered_fold(void) {
    mj_setup setup[5];
    small(setup);
    uint32_t sc[PT_DEFAULT_SCENE_WORDS];
    CHECK(masm_job_scene(setup, 5, sc, NULL));
    uint32_t tiles = pt_tiles(sc), passes = sc[PT_H_PASSES];
    CHECK(tiles == 6 && passes == 5);
    size_t nitems = (size_t)tiles * passes;
    uint8_t **bytes = mxalloc(nitems * sizeof *bytes);
    uint64_t *dig = mxalloc(nitems * sizeof *dig);
    for (size_t w = 0; w < nitems; w++) {
        bytes[w] = mxalloc(PT_PAYLOAD);
        dig[w] = payload(sc, (uint32_t)(w % tiles), (uint32_t)(w / tiles), bytes[w]);
    }
    uint32_t ts[6] = {0, 1, 2, 3, 4, 5};
    mesh_pt_tile ref[6], alt[6];
    CHECK(mesh_pt_tiles(sc, ts, tiles, passes, PT_TONEMAP_ACES, ref));
    CHECK(mesh_pt_tiles(sc, ts, tiles, passes, PT_TONEMAP_REINHARD, alt));
    uint32_t *ref_acc = mxalloc((size_t)tiles * 3 * PX * 4), *ref_rgb = mxalloc((size_t)tiles * PX * 4), *alt_rgb = mxalloc((size_t)tiles * PX * 4);
    for (uint32_t t = 0; t < tiles; t++) {
        memcpy(ref_acc + (size_t)t * 3 * PX, ref[t].acc, 3 * PX * 4);
        memcpy(ref_rgb + (size_t)t * PX, ref[t].rgb, PX * 4);
        memcpy(alt_rgb + (size_t)t * PX, alt[t].rgb, PX * 4);
    }
    for (uint64_t seed = 1; seed < 6; seed++) {
        masm *a = masm_new(9, setup, 5, PT_TONEMAP_ACES, NULL);
        size_t order[30];
        for (size_t i = 0; i < nitems; i++) order[i] = i;
        uint64_t r = seed;
        for (size_t i = nitems - 1; i >= 1; i--) {
            r ^= r << 13;
            r ^= r >> 7;
            r ^= r << 17;
            size_t j = (size_t)(r % (i + 1));
            size_t x = order[i];
            order[i] = order[j];
            order[j] = x;
        }
        /* Commits arrive in a random order too; every blob comes twice, some before their commit. */
        for (size_t k = 0; k < nitems; k++) {
            size_t i = order[k];
            masm_commit(a, i, (masm_item){.len = PT_PAYLOAD, .holders = 1, .member = (uint32_t)(k % 4), .part = dig[i]});
            masm_item junk = {.len = PT_PAYLOAD, .holders = 1, .member = 9, .part = 0};
            memset(junk.hash.b, 1, 32);
            masm_commit(a, i, junk);
            if (k % 3 == 0) CHECK(masm_blob(a, i, bytes[i], PT_PAYLOAD, NULL) >= 0);
        }
        for (size_t k = nitems; k-- > 0;) {
            size_t i = order[k];
            CHECK(masm_blob(a, i, bytes[i], PT_PAYLOAD, NULL) >= 0);
            CHECK(masm_blob(a, i, bytes[i], PT_PAYLOAD, NULL) >= 0);
        }
        uint64_t sum = 0, m;
        for (const uint64_t *c = momap_first(masm_by_member(a), &m); c; c = momap_next(masm_by_member(a), &m, &m)) sum += *c;
        CHECK(masm_complete(a) && masm_folded(a) == nitems && sum == masm_folded(a) && masm_bad_count(a) == 0);
        bool acc_same = true;
        for (uint32_t t = 0; t < tiles; t++) acc_same &= memcmp(masm_acc(a, t), ref_acc + (size_t)t * 3 * PX, 3 * PX * 4) == 0;
        CHECK(acc_same);
        CHECK(memcmp(masm_rgb(a), ref_rgb, (size_t)tiles * PX * 4) == 0);
        uint64_t da, dp;
        masm_digests(a, &da, &dp);
        CHECK(da == pt_fnv_words(ref_acc, (size_t)tiles * 3 * PX) && dp == pt_fnv_words(ref_rgb, (size_t)tiles * PX));
        masm_free(a);
    }
    /* A tone-map swap changes the pixels, never the accumulators; back again, the same bits. */
    masm *a = masm_new(9, setup, 5, 0, NULL);
    for (size_t w = 0; w < nitems; w++) {
        masm_commit(a, w, (masm_item){.len = PT_PAYLOAD, .holders = 1, .member = 0, .part = dig[w]});
        CHECK(masm_blob(a, w, bytes[w], PT_PAYLOAD, NULL) >= 0);
    }
    uint64_t a0, p0, a1, p1, a2, p2;
    masm_digests(a, &a0, &p0);
    masm_set_tone(a, PT_TONEMAP_REINHARD);
    masm_digests(a, &a1, &p1);
    CHECK(a1 == a0 && p1 == pt_fnv_words(alt_rgb, (size_t)tiles * PX) && p1 != p0);
    masm_set_tone(a, PT_TONEMAP_ACES);
    masm_digests(a, &a2, &p2);
    CHECK(a2 == a0 && p2 == p0);
    masm_free(a);
    /* A payload whose digest is not its record's is refused, never folded. */
    a = masm_new(9, setup, 5, 0, NULL);
    masm_commit(a, 0, (masm_item){.len = PT_PAYLOAD, .holders = 1, .member = 2, .part = dig[0]});
    uint8_t *b = mxalloc(PT_PAYLOAD);
    memcpy(b, bytes[0], PT_PAYLOAD);
    b[100] ^= 1;
    char *err = NULL;
    CHECK(masm_blob(a, 0, b, PT_PAYLOAD, &err) < 0 && masm_folded(a) == 0 && masm_bad_count(a) == 1);
    static const char want[] = "item 0 (tile 0 pass 0, system 2): pass-sum digest ";
    CHECK(err && strncmp(err, want, sizeof want - 1) == 0 && strstr(err, ", its record says ") != NULL);
    free(err);
    CHECK(masm_blob(a, 0, bytes[0], PT_PAYLOAD, NULL) >= 0 && masm_folded(a) == 1);
    /* A payload of the wrong length. */
    masm_commit(a, 1, (masm_item){.len = PT_PAYLOAD, .holders = 1, .member = 1, .part = dig[1]});
    CHECK(masm_blob(a, 1, bytes[1], 100, &err) < 0);
    CHECK_S(err, "item 1 (tile 1 pass 0, system 1): payload of 100 bytes, not 9216");
    free(err);
    masm_free(a);
    free(b);
    for (size_t w = 0; w < nitems; w++) free(bytes[w]);
    free(bytes);
    free(dig);
    for (uint32_t t = 0; t < tiles; t++) {
        mesh_pt_tile_free(&ref[t]);
        mesh_pt_tile_free(&alt[t]);
    }
    free(ref_acc);
    free(ref_rgb);
    free(alt_rgb);
}

static void default_reference(void) {
    masm *a = masm_new(1, NULL, 0, 0, NULL);
    CHECK(masm_width(a) == 640 && masm_height(a) == 360 && masm_tiles(a) == 300 && masm_passes(a) == 32 && masm_spp(a) == 8);
    masm_free(a);
    uint32_t sc[PT_DEFAULT_SCENE_WORDS];
    char *err = NULL;
    CHECK(!masm_job_scene((const mj_setup[]){{3, 0}}, 1, sc, &err));
    CHECK_S(err, "setup 3:0x0: not a scene setting (5:0, 7:<word << 24 | value>)");
    free(err);
    CHECK(!masm_job_scene((const mj_setup[]){{7, pt_set_arg(PT_H_WIDTH, 100)}}, 1, sc, &err));
    free(err);
    CHECK(masm_renders("pathtrace", PT_OP_PASS) && masm_renders("pathtrace-live", 254) && !masm_renders("pathtrace", 3) && !masm_renders("sha256d", 254));
    uint64_t part;
    uint8_t rec[32] = {0};
    rec[8] = 0x11;
    rec[15] = 0x88;
    CHECK(masm_part_of(rec, 32, &part) && part == 0x8800000000000011u);
    CHECK(!masm_part_of(rec, 15, &part));
}

int main(void) {
    ordered_fold();
    default_reference();
    return mesh_check_done();
}
