/* assemble.rs's Assembly, part_of, job_scene and renders. */
#include "mesh/assemble.h"
#include "mesh/abi/ops.h"
#include "mesh/assemble_bmp.h"
#include "mesh/ftz.h"
#include "mesh/ref/pathtrace.h"
#include "mesh/ref/sha256d.h"
#include "mesh/strbuf.h"
#include "pathtrace/digest.h"
#include "pathtrace/job_scene.h"
#include "pathtrace/scene.h"
#include "pathtrace/view.h"
#include "pathtrace/z.h"

#include <stdlib.h>
#include <string.h>

#define PX PT_TILE_PIX

typedef struct {
    uint64_t item;
    char *why;
} bad;

struct masm {
    mj_job_id job;
    uint32_t sc[PT_DEFAULT_SCENE_WORDS];
    uint32_t tiles, passes, tone;
    uint32_t *next;  /* per tile: the next pass to fold */
    uint32_t **acc;  /* per tile: the accumulators, once a pass is folded */
    uint32_t *rgb;   /* tiles x PX */
    bool *dirty;
    uint32_t *owner;
    momap *pending;  /* item -> masm_item: committed, not folded yet */
    momap *ready;    /* item -> uint32_t *: blobs fetched ahead of their turn */
    momap *by_member;
    uint64_t folded, committed;
    bad *bad;
    size_t nbad;
};

bool masm_part_of(const uint8_t *out, size_t n, uint64_t *part) {
    size_t at = 4 * (size_t)PT_J_PART;
    if (n < at + 8) return false;
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | out[at + (size_t)i];
    *part = v;
    return true;
}

bool masm_job_scene(const mj_setup *setup, size_t n, uint32_t *sc, char **err) {
    pt_setup_task *t = n ? mxalloc(n * sizeof *t) : NULL;
    for (size_t i = 0; i < n; i++) t[i] = (pt_setup_task){setup[i].op, setup[i].arg};
    long bad_at = pt_job_scene(t, n, sc);
    free(t);
    if (bad_at >= 0) {
        if (err)
            *err = mfmt("setup %u:0x%x: not a scene setting (5:0, 7:<word << 24 | value>)", setup[bad_at].op, setup[bad_at].arg);
        return false;
    }
    uint32_t rule = pt_validate_in(sc, PT_DEFAULT_SCENE_WORDS, PT_FLEET_MAX_TILES);
    if (rule) {
        if (err) *err = mfmt("the job's scene is not valid (rule %u)", rule);
        return false;
    }
    return true;
}

bool masm_renders(const char *program, uint32_t op) {
    return strncmp(program, "pathtrace", 9) == 0 && (op == PT_OP_PASS || op == MESH_OP_JOB);
}

static void ready_free(void *v) { free(*(uint32_t **)v); }

masm *masm_new(mj_job_id job, const mj_setup *setup, size_t nsetup, uint32_t tone, char **err) {
    masm *a = mxalloc(sizeof *a);
    if (!masm_job_scene(setup, nsetup, a->sc, err)) {
        free(a);
        return NULL;
    }
    a->job = job;
    a->tiles = pt_tiles(a->sc);
    a->passes = a->sc[PT_H_PASSES];
    a->tone = tone;
    a->next = mxalloc(a->tiles * sizeof *a->next);
    a->acc = mxalloc(a->tiles * sizeof *a->acc);
    a->rgb = mxalloc((size_t)a->tiles * PX * sizeof *a->rgb);
    a->dirty = mxalloc(a->tiles * sizeof *a->dirty);
    a->owner = mxalloc(a->tiles * sizeof *a->owner);
    for (uint32_t t = 0; t < a->tiles; t++) a->owner[t] = UINT32_MAX;
    a->pending = momap_new(1, sizeof(masm_item), NULL, NULL);
    a->ready = momap_new(1, sizeof(uint32_t *), ready_free, NULL);
    a->by_member = momap_new(1, sizeof(uint64_t), NULL, NULL);
    return a;
}

void masm_free(masm *a) {
    if (!a) return;
    for (uint32_t t = 0; t < a->tiles; t++) free(a->acc[t]);
    free(a->acc);
    free(a->next);
    free(a->rgb);
    free(a->dirty);
    free(a->owner);
    momap_free(a->pending);
    momap_free(a->ready);
    momap_free(a->by_member);
    for (size_t i = 0; i < a->nbad; i++) free(a->bad[i].why);
    free(a->bad);
    free(a);
}

mj_job_id masm_job(const masm *a) { return a->job; }
const uint32_t *masm_scene(const masm *a) { return a->sc; }
uint32_t masm_tiles(const masm *a) { return a->tiles; }
uint32_t masm_passes(const masm *a) { return a->passes; }
uint32_t masm_tone(const masm *a) { return a->tone; }
uint64_t masm_items(const masm *a) { return (uint64_t)a->tiles * a->passes; }
uint32_t masm_width(const masm *a) { return a->sc[PT_H_WIDTH]; }
uint32_t masm_height(const masm *a) { return a->sc[PT_H_HEIGHT]; }
uint32_t masm_spp(const masm *a) { return a->sc[PT_H_SPP]; }
bool masm_complete(const masm *a) { return a->folded == masm_items(a); }
uint64_t masm_folded(const masm *a) { return a->folded; }
uint64_t masm_committed(const masm *a) { return a->committed; }
const momap *masm_by_member(const masm *a) { return a->by_member; }
size_t masm_bad_count(const masm *a) { return a->nbad; }

const char *masm_bad_at(const masm *a, size_t i, uint64_t *item) {
    if (item) *item = a->bad[i].item;
    return a->bad[i].why;
}

uint32_t masm_tiles_done(const masm *a) {
    uint32_t n = 0;
    for (uint32_t t = 0; t < a->tiles; t++) n += a->next[t] == a->passes;
    return n;
}

void masm_tile(const masm *a, uint32_t t, uint32_t *passes, uint32_t *owner) {
    *passes = a->next[t];
    *owner = a->owner[t];
}

static void split(const masm *a, uint64_t item, uint32_t *t, uint32_t *p) {
    *t = (uint32_t)(item % a->tiles);
    *p = (uint32_t)(item / a->tiles);
}

static bool is_folded(const masm *a, uint64_t item) {
    uint32_t t, p;
    split(a, item, &t, &p);
    return p < a->next[t];
}

void masm_commit(masm *a, uint64_t item, masm_item it) {
    if (item >= masm_items(a) || is_folded(a, item) || momap_get1(a->pending, item)) return;
    *(masm_item *)momap_put1(a->pending, item, NULL) = it;
    a->committed++;
}

static bool is_bad(const masm *a, uint64_t item) {
    for (size_t i = 0; i < a->nbad; i++)
        if (a->bad[i].item == item) return true;
    return false;
}

size_t masm_wants(const masm *a, bool (*asked)(void *ctx, uint64_t item), void *ctx, size_t budget, uint64_t *items, masm_item *its) {
    /* Candidates by (pass, tile): every tile's window, collected then ordered. */
    typedef struct {
        uint32_t p, t;
        uint64_t item;
        masm_item it;
    } cand;
    cand *v = NULL;
    size_t n = 0;
    for (uint32_t t = 0; t < a->tiles; t++) {
        uint32_t nx = a->next[t], end = nx + MESH_ASSEMBLE_WINDOW < a->passes ? nx + MESH_ASSEMBLE_WINDOW : a->passes;
        for (uint32_t p = nx; p < end; p++) {
            uint64_t item = (uint64_t)p * a->tiles + t;
            const masm_item *it = momap_get1(a->pending, item);
            if (it && !momap_get1(a->ready, item) && !(asked && asked(ctx, item)) && !is_bad(a, item)) {
                v = mxrealloc(v, (n + 1) * sizeof *v);
                v[n++] = (cand){p, t, item, *it};
            }
        }
    }
    /* Insertion into (p, t) order: the tiles loop already gives t ascending within each p. */
    for (size_t i = 1; i < n; i++) {
        cand x = v[i];
        size_t j = i;
        while (j > 0 && (v[j - 1].p > x.p || (v[j - 1].p == x.p && v[j - 1].t > x.t))) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
    size_t k = n < budget ? n : budget;
    for (size_t i = 0; i < k; i++) {
        items[i] = v[i].item;
        if (its) its[i] = v[i].it;
    }
    free(v);
    return k;
}

const masm_item *masm_wanted(const masm *a, uint64_t item) { return momap_get1(a->pending, item); }

/* acc += P, word by word in the cluster float mode Z: pathtrace-core's fold_column add. */
static void fold(masm *a, uint32_t t, const uint32_t *part) {
    if (!a->acc[t]) a->acc[t] = mxalloc(3 * PX * sizeof(uint32_t));
    uint32_t *acc = a->acc[t];
    for (size_t i = 0; i < 3 * PX; i++) acc[i] = mesh_ftz_bits(mesh_ftz_add(mesh_ftz_from_bits(acc[i]), mesh_ftz_from_bits(part[i])));
    a->dirty[t] = true;
}

long masm_blob(masm *a, uint64_t item, const uint8_t *bytes, size_t n, char **err) {
    const masm_item *ip = momap_get1(a->pending, item);
    if (!ip) return 0;
    masm_item it = *ip;
    char *why = NULL;
    if (n != PT_PAYLOAD || it.len != PT_PAYLOAD) why = mfmt("payload of %zu bytes, not %u", n, (unsigned)PT_PAYLOAD);
    uint32_t *words = NULL;
    if (!why) {
        words = mxalloc(n);
        for (size_t i = 0; i < n / 4; i++)
            words[i] = (uint32_t)bytes[4 * i] | (uint32_t)bytes[4 * i + 1] << 8 | (uint32_t)bytes[4 * i + 2] << 16 | (uint32_t)bytes[4 * i + 3] << 24;
        uint64_t d = pt_payload_digest(words);
        if (d != it.part) why = mfmt("pass-sum digest %016llx, its record says %016llx", (unsigned long long)d, (unsigned long long)it.part);
    }
    if (why) {
        free(words);
        uint32_t t, p;
        split(a, item, &t, &p);
        char *e = mfmt("item %llu (tile %u pass %u, system %u): %s", (unsigned long long)item, t, p, it.member, why);
        free(why);
        a->bad = mxrealloc(a->bad, (a->nbad + 1) * sizeof *a->bad);
        a->bad[a->nbad++] = (bad){item, mstrdup(e)};
        if (err) *err = e;
        else free(e);
        return -1;
    }
    uint32_t **slot = momap_put1(a->ready, item, NULL);
    free(*slot);
    *slot = words;
    uint32_t t, p0;
    split(a, item, &t, &p0);
    long folded = 0;
    for (;;) {
        uint32_t p = a->next[t];
        if (p >= a->passes) break;
        uint64_t w = (uint64_t)p * a->tiles + t;
        uint32_t *ws = NULL;
        if (!momap_take(a->ready, &w, &ws)) break;
        masm_item done;
        momap_take(a->pending, &w, &done);
        fold(a, t, ws);
        free(ws);
        a->next[t] = p + 1;
        a->owner[t] = done.member;
        (*(uint64_t *)momap_put1(a->by_member, done.member, NULL))++;
        a->folded++;
        folded++;
    }
    return folded;
}

void masm_set_tone(masm *a, uint32_t tone) {
    if (tone != a->tone && tone < PT_TONEMAPS) {
        a->tone = tone;
        for (uint32_t t = 0; t < a->tiles; t++) a->dirty[t] = true;
    }
}

const uint32_t *masm_rgb(masm *a) {
    for (uint32_t t = 0; t < a->tiles; t++) {
        bool d = a->dirty[t];
        a->dirty[t] = false;
        if (!d || !a->acc[t]) continue;
        pt_v *v = mxalloc(PX * sizeof *v);
        for (size_t i = 0; i < PX; i++)
            v[i] = (pt_v){mesh_ftz_from_bits(a->acc[t][3 * i]), mesh_ftz_from_bits(a->acc[t][3 * i + 1]), mesh_ftz_from_bits(a->acc[t][3 * i + 2])};
        pt_tile_retone_z(a->sc, a->next[t], a->tone, v, a->rgb + (size_t)t * PX);
        free(v);
    }
    return a->rgb;
}

const uint32_t *masm_acc(const masm *a, uint32_t t) { return a->acc[t]; }

void masm_digests(masm *a, uint64_t *acc, uint64_t *pixels) {
    uint64_t h = PT_FNV_OFFSET;
    for (uint32_t t = 0; t < a->tiles; t++)
        for (size_t i = 0; i < 3 * PX; i++) h = pt_fnv(h, a->acc[t] ? a->acc[t][i] : 0);
    *acc = h;
    *pixels = pt_fnv_words(masm_rgb(a), (size_t)a->tiles * PX);
}

uint8_t *masm_raster(masm *a, size_t *len) {
    size_t n = (size_t)masm_width(a) * masm_height(a) * 3;
    uint8_t *r = mxalloc(n);
    mesh_pt_raster(a->sc, masm_rgb(a), r);
    *len = n;
    return r;
}

uint8_t *masm_ppm(masm *a, size_t *len) {
    size_t rn;
    uint8_t *r = masm_raster(a, &rn);
    uint8_t *p = mesh_pt_ppm(masm_width(a), masm_height(a), r, len);
    free(r);
    return p;
}

uint8_t *masm_bmp_of(masm *a, size_t *len) {
    size_t rn;
    uint8_t *r = masm_raster(a, &rn);
    uint8_t *b = masm_bmp(masm_width(a), masm_height(a), r, len);
    free(r);
    return b;
}

char *masm_sha256(masm *a) {
    size_t n;
    uint8_t *p = masm_ppm(a, &n);
    uint8_t h[32];
    mesh_sha256(p, n, h);
    free(p);
    char *x = mxalloc(65);
    mesh_sha_hex(h, 32, x);
    return x;
}
