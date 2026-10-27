/* load.rs's parsers, line for line. */
#include "mesh/load_proc.h"
#include "mesh/abi/host_load.h"
#include "mesh/buf.h"
#include "mesh/rsfmt.h"

#include <stdlib.h>
#include <string.h>

void mesh_cpu_times_since(const mesh_cpu_times *now, const mesh_cpu_times *prev, float *busy, float *io) {
    uint64_t dt = now->total > prev->total ? now->total - prev->total : 0;
    if (dt == 0) { *busy = 0; *io = 0; return; }
    uint64_t b = now->busy > prev->busy ? now->busy - prev->busy : 0;
    uint64_t i = now->iowait > prev->iowait ? now->iowait - prev->iowait : 0;
    if (b > dt) b = dt;
    if (i > dt) i = dt;
    *busy = (float)b / (float)dt;
    *io = (float)i / (float)dt;
}

/* The next line of text (without its \n); NULL at the end. */
static const char *next_line(const char **p, size_t *n) {
    if (!**p) return NULL;
    const char *l = *p, *e = strchr(l, '\n');
    *n = e ? (size_t)(e - l) : strlen(l);
    *p = e ? e + 1 : l + *n;
    if (*n && l[*n - 1] == '\r') (*n)--; /* str::lines */
    return l;
}

static bool is_ws(char c) { return c == ' ' || (c >= 9 && c <= 13); }

/* split_whitespace over [s, s+n): the next word. */
static bool word(const char **s, const char *end, const char **w, size_t *wn) {
    while (*s < end && is_ws(**s)) (*s)++;
    if (*s >= end) return false;
    *w = *s;
    while (*s < end && !is_ws(**s)) (*s)++;
    *wn = (size_t)(*s - *w);
    return true;
}

static char *quoted(const char *s, size_t n) {
    mesh_buf b = MESH_BUF_INIT;
    mesh_rs_debug_str(&b, s, n);
    return mesh_buf_take(&b);
}

int mesh_parse_proc_stat(const char *text, mesh_cpu_times *all, mesh_cpu_times *per, bool *have, size_t *n, char **err) {
    bool got_all = false;
    *n = 0;
    for (size_t i = 0; i < MESH_MAX_CPUS; i++) have[i] = false;
    const char *p = text, *l;
    size_t ln;
    while ((l = next_line(&p, &ln))) {
        if (ln < 3 || strncmp(l, "cpu", 3) != 0) continue;
        const char *rest = l + 3, *end = l + ln;
        const char *sp = memchr(rest, ' ', (size_t)(end - rest));
        char *q = quoted(l, ln);
        if (!sp) { *err = mesh_buf_fmt("/proc/stat: %s", q); free(q); return -1; }
        uint64_t v[16];
        size_t k = 0;
        const char *s = sp + 1, *w;
        size_t wn;
        while (word(&s, end, &w, &wn)) {
            uint64_t x;
            int r = mesh_rs_parse_u64(w, wn, &x);
            if (r) { *err = mesh_buf_fmt("/proc/stat %s: %s", q, mesh_rs_parse_error(r)); free(q); return -1; }
            if (k < 16) v[k] = x;
            k++;
        }
        if (k < 8) { *err = mesh_buf_fmt("/proc/stat %s: %zu fields, want at least 8", q, k); free(q); return -1; }
        uint64_t total = 0;
        for (int i = 0; i < 8; i++) total += v[i];
        mesh_cpu_times t = {total - v[3] - v[4], total, v[4]};
        size_t idn = (size_t)(sp - rest);
        if (idn == 0) { *all = t; got_all = true; }
        else {
            uint64_t id;
            if (mesh_rs_parse_u64(rest, idn, &id)) {
                char *qi = quoted(rest, idn);
                *err = mesh_buf_fmt("/proc/stat: cpu id %s", qi);
                free(qi);
                free(q);
                return -1;
            }
            if (id < MESH_MAX_CPUS) {
                if (*n <= id) *n = (size_t)id + 1;
                per[id] = t;
                have[id] = true;
            }
        }
        free(q);
    }
    if (!got_all) { *err = mesh_xstrdup("/proc/stat has no cpu line"); return -1; }
    if (*n == 0) { *err = mesh_xstrdup("/proc/stat lists no cpuN line"); return -1; }
    return 0;
}

void mesh_classify(const uint64_t *perf, const bool *have, size_t n, uint8_t *out) {
    uint64_t max = 0;
    for (size_t i = 0; i < n; i++) if (have[i] && perf[i] > max) max = perf[i];
    for (size_t i = 0; i < n; i++) {
        if (max == 0) out[i] = MESH_CORE_UNKNOWN;
        else if (!have[i]) out[i] = MESH_CORE_UNKNOWN;
        else out[i] = perf[i] * 10 >= max * 9 ? MESH_CORE_BIG : MESH_CORE_LITTLE;
    }
}

static float class_mean(const float *util, const bool *have, const uint8_t *cls, size_t n, int want) {
    float sum = 0;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (want >= 0 && cls[i] != want) continue;
        if (!have[i]) continue;
        sum += util[i];
        k++;
    }
    return k ? sum / (float)k : 0.0f;
}

void mesh_class_util(const float *util, const bool *have, const uint8_t *cls, size_t n, float *all, float *big, float *little) {
    *all = class_mean(util, have, cls, n, -1);
    *big = class_mean(util, have, cls, n, MESH_CORE_BIG);
    *little = class_mean(util, have, cls, n, MESH_CORE_LITTLE);
}

int mesh_parse_psi(const char *text, mesh_psi *out, char **err) {
    memset(out, 0, sizeof *out);
    bool some = false;
    const char *p = text, *l;
    size_t ln;
    while ((l = next_line(&p, &ln))) {
        const char *s = l, *end = l + ln, *w;
        size_t wn;
        const char *kind = "";
        size_t kn = 0;
        if (word(&s, end, &w, &wn)) { kind = w; kn = wn; }
        bool ha = false, ht = false;
        float avg10 = 0;
        uint64_t total = 0;
        while (word(&s, end, &w, &wn)) {
            const char *eq = memchr(w, '=', wn);
            if (!eq) continue;
            size_t keyn = (size_t)(eq - w), vn = wn - keyn - 1;
            if (keyn == 5 && !strncmp(w, "avg10", 5)) {
                float a;
                ha = mesh_rs_parse_f32(eq + 1, vn, &a) == 0;
                if (ha) avg10 = a;
            } else if (keyn == 5 && !strncmp(w, "total", 5)) {
                uint64_t t;
                ht = mesh_rs_parse_u64(eq + 1, vn, &t) == 0;
                if (ht) total = t;
            }
        }
        char *q = quoted(l, ln);
        if (!ha || !ht) { *err = mesh_buf_fmt("PSI line %s: no avg10/total", q); free(q); return -1; }
        if (kn == 4 && !strncmp(kind, "some", 4)) { out->some_avg10 = avg10 / 100.0f; out->some_total = total; some = true; }
        else if (kn == 4 && !strncmp(kind, "full", 4)) { out->full_avg10 = avg10 / 100.0f; out->full_total = total; }
        else { *err = mesh_buf_fmt("PSI line %s", q); free(q); return -1; }
        free(q);
    }
    if (!some) { *err = mesh_xstrdup("PSI: no `some` line"); return -1; }
    return 0;
}

float mesh_psi_rate(uint64_t now, uint64_t prev, uint64_t dt_us) {
    if (dt_us == 0) return 0;
    double r = (double)(now > prev ? now - prev : 0) / (double)dt_us;
    return (float)(r < 1.0 ? r : 1.0);
}

int mesh_parse_meminfo(const char *text, mesh_meminfo *m, char **err) {
    memset(m, 0, sizeof *m);
    int have = 0;
    const char *p = text, *l;
    size_t ln;
    while ((l = next_line(&p, &ln))) {
        const char *colon = memchr(l, ':', ln);
        if (!colon) continue;
        size_t kn = (size_t)(colon - l);
        uint64_t *dst = NULL;
        int bit = 0;
        if (kn == 8 && !strncmp(l, "MemTotal", 8)) { dst = &m->total_kb; bit = 1; }
        else if (kn == 12 && !strncmp(l, "MemAvailable", 12)) { dst = &m->avail_kb; bit = 2; }
        else if (kn == 9 && !strncmp(l, "SwapTotal", 9)) dst = &m->swap_total_kb;
        else if (kn == 8 && !strncmp(l, "SwapFree", 8)) dst = &m->swap_free_kb;
        if (!dst) continue;
        const char *s = colon + 1, *w;
        size_t wn;
        uint64_t x;
        if (!word(&s, l + ln, &w, &wn) || mesh_rs_parse_u64(w, wn, &x)) {
            char *q = quoted(l, ln);
            *err = mesh_buf_fmt("/proc/meminfo %s", q);
            free(q);
            return -1;
        }
        *dst = x;
        have |= bit;
    }
    if (have != 3) { *err = mesh_xstrdup("/proc/meminfo: no MemTotal or MemAvailable"); return -1; }
    return 0;
}

int mesh_parse_loadavg(const char *text, float out[3], char **err) {
    const char *s = text, *end = text + strlen(text), *w;
    size_t wn, k = 0;
    while (k < 3 && word(&s, end, &w, &wn)) {
        int r = mesh_rs_parse_f32(w, wn, &out[k]);
        if (r) {
            char *q = quoted(text, strlen(text));
            *err = mesh_buf_fmt("/proc/loadavg %s: %s", q, mesh_rs_parse_error(r));
            free(q);
            return -1;
        }
        k++;
    }
    if (k != 3) {
        char *q = quoted(text, strlen(text));
        *err = mesh_buf_fmt("/proc/loadavg %s", q);
        free(q);
        return -1;
    }
    return 0;
}

bool mesh_proc_field(const char *text, const char *key, uint64_t *out) {
    const char *p = text, *l;
    size_t ln, keyn = strlen(key);
    while ((l = next_line(&p, &ln))) {
        /* split_once(':') or else split_once(' ') */
        const char *sep = memchr(l, ':', ln);
        if (!sep) sep = memchr(l, ' ', ln);
        if (!sep) continue;
        const char *ks = l, *ke = sep;
        while (ks < ke && is_ws(*ks)) ks++;
        while (ke > ks && is_ws(ke[-1])) ke--;
        if ((size_t)(ke - ks) != keyn || strncmp(ks, key, keyn)) continue;
        const char *s = sep + 1, *w;
        size_t wn;
        uint64_t x;
        if (!word(&s, l + ln, &w, &wn) || mesh_rs_parse_u64(w, wn, &x)) return false;
        *out = x;
        return true;
    }
    return false;
}
