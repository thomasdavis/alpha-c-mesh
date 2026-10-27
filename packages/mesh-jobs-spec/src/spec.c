/* jobs.rs's Mode and JobSpec. */
#include "mesh/jobs/spec.h"
#include "mesh/debug_str.h"
#include "mesh/rs_parse.h"

#include <stdlib.h>
#include <string.h>

const char *mj_mode_word(mj_mode m) {
    switch (m) {
    case MJ_MODE_COLLECT: return "collect";
    case MJ_MODE_NONZERO: return "nonzero";
    default: return "none";
    }
}

bool mj_mode_parse(const char *s, mj_mode *out) {
    if (strcmp(s, "collect") == 0) *out = MJ_MODE_COLLECT;
    else if (strcmp(s, "nonzero") == 0) *out = MJ_MODE_NONZERO;
    else if (strcmp(s, "none") == 0) *out = MJ_MODE_NONE;
    else return false;
    return true;
}

mj_setup *mj_setup_clone(const mj_setup *s, size_t n) {
    if (!n) return NULL;
    mj_setup *c = mxalloc(n * sizeof *c);
    memcpy(c, s, n * sizeof *c);
    return c;
}

void mj_spec_free(mj_spec *s) {
    free(s->program);
    mj_ranges_free(&s->items);
    free(s->setup);
    memset(s, 0, sizeof *s);
}

mj_spec mj_spec_clone(const mj_spec *s) {
    mj_spec c = *s;
    c.program = mstrdup(s->program ? s->program : "");
    c.items = mj_ranges_clone(&s->items);
    c.setup = mj_setup_clone(s->setup, s->nsetup);
    return c;
}

bool mj_spec_eq(const mj_spec *a, const mj_spec *b) {
    return a->id == b->id && strcmp(a->program, b->program) == 0 && a->op == b->op && mj_ranges_eq(&a->items, &b->items) &&
           a->priority == b->priority && a->attempts == b->attempts && a->deadline_ms == b->deadline_ms && a->mode == b->mode &&
           a->nsetup == b->nsetup && (a->nsetup == 0 || memcmp(a->setup, b->setup, a->nsetup * sizeof *a->setup) == 0) &&
           a->submitted_ms == b->submitted_ms && a->members == b->members;
}

bool mj_spec_endless(const mj_spec *s) {
    uint64_t e;
    return mj_ranges_end(&s->items, &e) && e == UINT64_MAX;
}

bool mj_spec_runs(const mj_spec *s, const char *name) {
    size_t n = strlen(name);
    const char *p = s->program;
    for (;;) {
        const char *c = strchr(p, ',');
        size_t len = c ? (size_t)(c - p) : strlen(p);
        if (len == n && memcmp(p, name, n) == 0) return true;
        if (!c) return false;
        p = c + 1;
    }
}

size_t mj_spec_first_program(const mj_spec *s, const char **p) {
    *p = s->program;
    const char *c = strchr(s->program, ',');
    return c ? (size_t)(c - s->program) : strlen(s->program);
}

void mj_spec_encode(const mj_spec *s, msb *out) {
    msb_printf(out, "id %llu program %s op %u items ", (unsigned long long)s->id, s->program, s->op);
    mj_ranges_word(&s->items, out);
    msb_printf(out, " priority %d attempts %u deadline %llu mode %s setup ", s->priority, s->attempts,
               (unsigned long long)s->deadline_ms, mj_mode_word(s->mode));
    if (!s->nsetup) msb_putc(out, '-');
    for (size_t i = 0; i < s->nsetup; i++) msb_printf(out, "%s%u:%u", i ? "," : "", s->setup[i].op, s->setup[i].arg);
    msb_printf(out, " submitted %llu members 0x%x", (unsigned long long)s->submitted_ms, s->members);
}

/* Rust's char::is_whitespace for ASCII (what split_whitespace splits on). */
static bool ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

typedef struct {
    const char *p;
    size_t n;
} word;

static size_t split(const char *t, word *w, size_t max) {
    size_t k = 0;
    const char *p = t;
    while (*p) {
        while (*p && ws(*p)) p++;
        if (!*p) break;
        const char *s = p;
        while (*p && !ws(*p)) p++;
        if (k < max) w[k] = (word){s, (size_t)(p - s)};
        k++;
    }
    return k;
}

static bool is(word w, const char *s) { return strlen(s) == w.n && memcmp(w.p, s, w.n) == 0; }

static bool bad(const char *t, char **err) {
    if (err) {
        msb b = {0};
        msb_puts(&b, "job spec ");
        mdebug_str(&b, t, strlen(t));
        *err = msb_take(&b);
    }
    return false;
}

/* `o:a,o:a`: each a decimal u32 pair. */
static bool parse_setup(const char *p, size_t n, mj_setup **out, size_t *count) {
    mj_setup *v = NULL;
    size_t k = 0;
    const char *end = p + n;
    for (;;) {
        const char *c = memchr(p, ',', (size_t)(end - p));
        const char *e = c ? c : end;
        const char *colon = memchr(p, ':', (size_t)(e - p));
        uint32_t o, a;
        if (!colon || !rs_u32(p, (size_t)(colon - p), &o) || !rs_u32(colon + 1, (size_t)(e - colon - 1), &a)) {
            free(v);
            return false;
        }
        v = mxrealloc(v, (k + 1) * sizeof *v);
        v[k++] = (mj_setup){o, a};
        if (!c) break;
        p = c + 1;
    }
    *out = v;
    *count = k;
    return true;
}

bool mj_spec_decode(const char *t, mj_spec *out, char **err) {
    static const char *const keys[] = {"id", "program", "op", "items", "priority", "attempts", "deadline",
                                       "mode", "setup", "submitted", "members"};
    word w[22];
    if (split(t, w, 22) != 22) return bad(t, err);
    for (int i = 0; i < 11; i++)
        if (!is(w[2 * i], keys[i])) return bad(t, err);
    mj_spec s = {0};
    /* In the Rust's order: members first, then the rest as written. */
    word mm = w[21];
    while (mm.n >= 2 && memcmp(mm.p, "0x", 2) == 0) mm.p += 2, mm.n -= 2;
    uint64_t v;
    if (!rs_parse_uint(mm.p, mm.n, 16, UINT32_MAX, &v)) return bad(t, err);
    s.members = (uint32_t)v;
    if (!rs_u64(w[1].p, w[1].n, &s.id)) return bad(t, err);
    s.program = mstrndup(w[3].p, w[3].n);
    char *it = mstrndup(w[7].p, w[7].n);
    bool ok = rs_u32(w[5].p, w[5].n, &s.op);
    if (!ok) {
        free(it);
        mj_spec_free(&s);
        return bad(t, err);
    }
    ok = mj_ranges_parse(it, &s.items, err);
    free(it);
    if (!ok) {
        mj_spec_free(&s);
        return false;
    }
    char *mode = mstrndup(w[15].p, w[15].n);
    ok = rs_i32(w[9].p, w[9].n, &s.priority) && rs_u32(w[11].p, w[11].n, &s.attempts) && rs_u64(w[13].p, w[13].n, &s.deadline_ms) &&
         mj_mode_parse(mode, &s.mode) && (is(w[17], "-") || parse_setup(w[17].p, w[17].n, &s.setup, &s.nsetup)) &&
         rs_u64(w[19].p, w[19].n, &s.submitted_ms);
    free(mode);
    if (!ok) {
        mj_spec_free(&s);
        return bad(t, err);
    }
    *out = s;
    return true;
}
