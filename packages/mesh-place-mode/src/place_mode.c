/* place.rs: Assign, Mode, Target, Role and their rules. */
#include "mesh/place_mode.h"
#include "mesh/abi/base.h"
#include "mesh/parse.h"
#include "mesh/rust_fmt.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <string.h>

uint32_t mpm_bits(uint32_t n) { return n >= 32 ? UINT32_MAX : (1u << n) - 1; }

mpm_assign mpm_assign_new(const uint32_t *of, size_t n) {
    mpm_assign a;
    a.fanout = (uint32_t)n;
    memset(a.of, MESH_PART_NONE, sizeof a.of);
    for (size_t b = 0; b < n && b < MESH_PLACE_PARTS_MAX; b++) a.of[b] = (uint8_t)of[b];
    return a;
}

uint32_t mpm_owner(const mpm_assign *a) { return a->of[0]; }

uint32_t mpm_mask_of(const mpm_assign *a, uint32_t sys) {
    uint32_t m = 0;
    for (uint32_t b = 0; b < a->fanout; b++)
        if (a->of[b] == sys) m |= 1u << b;
    return m;
}

uint32_t mpm_members(const mpm_assign *a) {
    uint32_t m = 0;
    for (uint32_t b = 0; b < a->fanout; b++) m |= 1u << a->of[b];
    return m;
}

char *mpm_list(const mpm_assign *a) {
    mt_buf b = {0};
    for (uint32_t i = 0; i < a->fanout; i++) mt_printf(&b, i ? ",%u" : "%u", a->of[i]);
    return mt_take(&b);
}

mpm_assign mpm_counts(const uint32_t *sys, const uint32_t *count, size_t n) {
    uint32_t of[MESH_PLACE_PARTS_MAX];
    size_t k = 0;
    for (size_t i = 0; i < n; i++)
        for (uint32_t j = 0; j < count[i] && k < MESH_PLACE_PARTS_MAX; j++) of[k++] = sys[i];
    return mpm_assign_new(of, k);
}

mpm_assign mpm_even(const uint32_t *systems, size_t n, uint32_t fanout) {
    uint32_t k = (uint32_t)n < fanout ? (uint32_t)n : fanout;
    if (k < 1) k = 1;
    uint32_t count[MESH_PLACE_PARTS_MAX];
    for (uint32_t i = 0; i < k; i++) count[i] = fanout / k + (i < fanout % k);
    return mpm_counts(systems, count, k);
}

mpm_assign mpm_auto(const uint32_t *sys, const uint32_t *ns, size_t n, uint32_t fanout) {
    uint64_t sum = 0, known = 0;
    for (size_t i = 0; i < n; i++)
        if (ns[i] > 0) sum += ns[i], known++;
    uint64_t fill = known ? sum / known : 1;
    uint64_t t[MESH_PLACE_PARTS_MAX];
    uint32_t count[MESH_PLACE_PARTS_MAX] = {0};
    for (size_t i = 0; i < n; i++) t[i] = ns[i] > 0 ? ns[i] : fill;
    for (uint32_t p = 0; p < fanout; p++) {
        size_t best = 0;
        for (size_t i = 1; i < n; i++) {
            uint64_t ki = (count[i] + 1ull) * t[i], kb = (count[best] + 1ull) * t[best];
            if (ki < kb || (ki == kb && sys[i] < sys[best])) best = i;
        }
        count[best]++;
    }
    size_t fastest = n;
    for (size_t i = 0; i < n; i++)
        if (count[i] > 0 && (fastest == n || t[i] < t[fastest] || (t[i] == t[fastest] && sys[i] < sys[fastest]))) fastest = i;
    uint32_t os[MESH_PLACE_PARTS_MAX], oc[MESH_PLACE_PARTS_MAX];
    size_t k = 0;
    os[k] = sys[fastest], oc[k++] = count[fastest];
    for (size_t i = 0; i < n; i++)
        if (i != fastest && count[i] > 0) os[k] = sys[i], oc[k++] = count[i];
    return mpm_counts(os, oc, k);
}

bool mpm_assign_eq(const mpm_assign *a, const mpm_assign *b) { return a->fanout == b->fanout && memcmp(a->of, b->of, sizeof a->of) == 0; }

static bool check(const mpm_assign *a, uint32_t n, char **err) {
    if (a->fanout == 0 || a->fanout > MESH_PLACE_PARTS_MAX) {
        *err = mt_fmt("split of %u parts", a->fanout);
        return false;
    }
    for (uint32_t b = 0; b < a->fanout; b++)
        if (a->of[b] >= n) {
            *err = mt_fmt("split: part %u to system %u, the cluster has systems 0..%u", b, a->of[b], n - 1);
            return false;
        }
    return true;
}

mpm_mode mpm_local(void) { return (mpm_mode){.kind = MPM_LOCAL}; }
mpm_mode mpm_pin(uint32_t k) { return (mpm_mode){.kind = MPM_PIN, .pin = k}; }
mpm_mode mpm_split(mpm_assign a) { return (mpm_mode){.kind = MPM_SPLIT, .a = a}; }

bool mpm_mode_eq(const mpm_mode *a, const mpm_mode *b) {
    if (a->kind != b->kind) return false;
    if (a->kind == MPM_PIN) return a->pin == b->pin;
    if (a->kind == MPM_SPLIT) return mpm_assign_eq(&a->a, &b->a);
    return true;
}

bool mpm_target_eq(const mpm_target *a, const mpm_target *b) {
    if (a->is_auto != b->is_auto) return false;
    return a->is_auto ? a->auto_mask == b->auto_mask : mpm_mode_eq(&a->mode, &b->mode);
}

static char *usage(char *const *words, size_t nwords, uint32_t n) {
    mt_strs w = {0};
    for (size_t i = 0; i < nwords; i++) mt_push(&w, words[i]);
    char *j = mt_join(&w, " ");
    char *q = rf_debug_str(j);
    char *e = mt_fmt("usage: place <name> [local | pin 0..%u | split [auto | a,b,.. | a:parts,b:parts,..]], not %s", n - 1, q);
    free(j), free(q);
    mt_strs_free(&w);
    return e;
}

static bool sys_word(const char *w, uint32_t n, uint32_t *k, char **err) {
    if (mp_u32(w, k) && *k < n) return true;
    char *q = rf_debug_str(w);
    *err = mt_fmt("%s: not a system of this cluster (0..%u)", q, n - 1);
    free(q);
    return false;
}

static bool split_spec(const char *spec, uint32_t n, uint32_t fanout, mpm_target *out, char **err, char *const *words, size_t nwords) {
    mt_strs items = mt_split(spec, ',');
    bool colon = false, ok = false;
    for (size_t i = 0; i < items.n; i++)
        if (strchr(items.v[i], ':')) colon = true;
    uint32_t ks[64], ps[64];
    size_t c = 0;
    if (items.n > 64) {
        *err = mt_fmt("split %s: %zu systems for %u parts", spec, items.n, fanout);
        goto done;
    }
    if (colon) {
        for (size_t i = 0; i < items.n; i++) {
            char *k, *p;
            if (!mt_split_once(items.v[i], ':', &k, &p)) {
                *err = usage(words, nwords, n);
                goto done;
            }
            uint32_t parts;
            bool pok = mp_u32(p, &parts);
            if (!pok || parts == 0) {
                char *q = rf_debug_str(items.v[i]);
                *err = mt_fmt(pok ? "%s: at least one part" : "%s: system:parts", q);
                free(q), free(k), free(p);
                goto done;
            }
            uint32_t s;
            bool sok = sys_word(k, n, &s, err);
            free(k), free(p);
            if (!sok) goto done;
            ks[c] = s, ps[c++] = parts;
        }
        uint32_t total = 0;
        for (size_t i = 0; i < c; i++) total += ps[i];
        if (total != fanout) {
            *err = mt_fmt("split %s: %u parts, the program has %u", spec, total, fanout);
            goto done;
        }
        for (size_t i = 1; i < c; i++)
            for (size_t j = 0; j < i; j++)
                if (ks[j] == ks[i]) {
                    *err = mt_fmt("split %s: a system twice", spec);
                    goto done;
                }
        *out = (mpm_target){.mode = mpm_split(mpm_counts(ks, ps, c))};
        ok = true;
    } else {
        for (size_t i = 0; i < items.n; i++) {
            if (!sys_word(items.v[i], n, &ks[c], err)) goto done;
            c++;
        }
        if (c == fanout) *out = (mpm_target){.mode = mpm_split(mpm_assign_new(ks, c))};
        else if (c > fanout) {
            *err = mt_fmt("split %s: %zu systems for %u parts", spec, c, fanout);
            goto done;
        } else {
            for (size_t i = 1; i < c; i++)
                for (size_t j = 0; j < i; j++)
                    if (ks[j] == ks[i]) {
                        *err = mt_fmt("split %s: a system twice (list %u systems to give one per part)", spec, fanout);
                        goto done;
                    }
            *out = (mpm_target){.mode = mpm_split(mpm_even(ks, c, fanout))};
        }
        ok = true;
    }
done:
    mt_strs_free(&items);
    return ok;
}

bool mpm_parse(char *const *words, size_t nwords, uint32_t n, uint32_t fanout, mpm_target *out, char **err) {
    if (nwords == 1 && strcmp(words[0], "local") == 0) return *out = (mpm_target){.mode = mpm_local()}, true;
    if (nwords == 2 && strcmp(words[0], "pin") == 0) {
        uint32_t k;
        if (!sys_word(words[1], n, &k, err)) return false;
        return *out = (mpm_target){.mode = mpm_pin(k)}, true;
    }
    if (nwords == 1 && strcmp(words[0], "split") == 0) {
        uint32_t all[MESH_PLACE_PARTS_MAX];
        for (uint32_t k = 0; k < n && k < MESH_PLACE_PARTS_MAX; k++) all[k] = k;
        return *out = (mpm_target){.mode = mpm_split(mpm_even(all, n, fanout))}, true;
    }
    if (nwords == 2 && strcmp(words[0], "split") == 0 && strcmp(words[1], "auto") == 0) return *out = (mpm_target){.is_auto = true, .auto_mask = mpm_bits(n)}, true;
    if (nwords == 2 && strcmp(words[0], "split") == 0) return split_spec(words[1], n, fanout, out, err, words, nwords);
    *err = usage(words, nwords, n);
    return false;
}

void mpm_code(const mpm_mode *m, uint32_t *code, uint32_t *sys) {
    switch (m->kind) {
    case MPM_LOCAL: *code = MESH_PLACE_LOCAL, *sys = 0; break;
    case MPM_PIN: *code = MESH_PLACE_PIN, *sys = m->pin; break;
    default: *code = MESH_PLACE_SPLIT, *sys = mpm_owner(&m->a);
    }
}

void mpm_assign_bytes(const mpm_mode *m, uint8_t out[MESH_PLACE_PARTS_MAX]) {
    if (m->kind == MPM_SPLIT) memcpy(out, m->a.of, MESH_PLACE_PARTS_MAX);
    else memset(out, MESH_PART_NONE, MESH_PLACE_PARTS_MAX);
}

bool mpm_from_code(uint32_t code, uint32_t sys, const uint8_t *assign, size_t n, mpm_mode *out, char **err) {
    if (code == MESH_PLACE_LOCAL) return *out = mpm_local(), true;
    if (code == MESH_PLACE_PIN && sys < MESH_MAX_SYSTEMS) return *out = mpm_pin(sys), true;
    if (code == MESH_PLACE_SPLIT) {
        size_t fanout = n;
        for (size_t i = 0; i < n; i++)
            if (assign[i] == MESH_PART_NONE) {
                fanout = i;
                break;
            }
        uint32_t of[MESH_PLACE_PARTS_MAX];
        for (size_t i = 0; i < fanout && i < MESH_PLACE_PARTS_MAX; i++) of[i] = assign[i];
        mpm_assign a = mpm_assign_new(of, fanout);
        if (!check(&a, MESH_MAX_SYSTEMS, err)) return false;
        for (size_t i = fanout; i < n; i++)
            if (assign[i] != MESH_PART_NONE) {
                *err = mt_strdup("split: a gap in the assignment");
                return false;
            }
        if (mpm_owner(&a) != sys) {
            *err = mt_fmt("split: owner %u, part 0 is system %u's", sys, mpm_owner(&a));
            return false;
        }
        *out = mpm_split(a);
        return true;
    }
    *err = mt_fmt("bad placement %u/%u", code, sys);
    return false;
}

char *mpm_label(const mpm_mode *m) {
    if (m->kind == MPM_LOCAL) return mt_strdup("local");
    if (m->kind == MPM_PIN) return mt_fmt("pin %u", m->pin);
    char *l = mpm_list(&m->a), *s = mt_fmt("split %s", l);
    free(l);
    return s;
}

bool mpm_allowed(const mpm_mode *m, uint32_t cap, char **err) {
    if (cap == MESH_PLACE_CAP_NONE) {
        *err = mt_strdup("declares no `place` in its manifest");
        return false;
    }
    if (m->kind == MPM_SPLIT && cap == MESH_PLACE_CAP_PIN) {
        *err = mt_strdup("declares place = \"pin\" only (no split)");
        return false;
    }
    return true;
}

bool mpm_fits(const mpm_mode *m, uint32_t n, uint32_t fanout, char **err) {
    if (m->kind == MPM_LOCAL) return true;
    if (m->kind == MPM_PIN) {
        if (m->pin < n) return true;
        *err = mt_fmt("pin %u: the cluster has systems 0..%u", m->pin, n - 1);
        return false;
    }
    if (m->a.fanout != fanout) {
        *err = mt_fmt("split of %u parts, the program has %u", m->a.fanout, fanout);
        return false;
    }
    return check(&m->a, n, err);
}

mpm_role mpm_role_of(const mpm_mode *m, uint32_t me) {
    switch (m->kind) {
    case MPM_LOCAL: return MPM_ROLE_LOCAL;
    case MPM_PIN: return m->pin == me ? MPM_ROLE_RUNNER : MPM_ROLE_MIRROR;
    default:
        if (mpm_owner(&m->a) == me) return MPM_ROLE_OWNER;
        return mpm_mask_of(&m->a, me) ? MPM_ROLE_FOLLOWER : MPM_ROLE_MIRROR;
    }
}

bool mpm_runs(mpm_role r) { return r != MPM_ROLE_MIRROR; }

uint32_t mpm_input_to(const mpm_mode *m, uint32_t me) {
    return m->kind == MPM_LOCAL ? me : m->kind == MPM_PIN ? m->pin : mpm_owner(&m->a);
}

uint32_t mpm_parts_of(const mpm_mode *m, uint32_t sys, uint32_t fanout) {
    if (m->kind == MPM_LOCAL) return 0;
    if (m->kind == MPM_PIN) return m->pin == sys ? mpm_bits(fanout) : 0;
    return mpm_mask_of(&m->a, sys);
}

uint32_t mpm_authority(const mpm_mode *from, const mpm_mode *to) {
    if (from->kind == MPM_PIN) return from->pin;
    if (from->kind == MPM_SPLIT) return mpm_owner(&from->a);
    if (to->kind == MPM_PIN) return to->pin;
    if (to->kind == MPM_SPLIT) return mpm_owner(&to->a);
    return 0;
}
