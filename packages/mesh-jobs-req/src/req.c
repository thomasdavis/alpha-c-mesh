/* jobs.rs's Submit::parse and Req::parse. */
#include "mesh/jobs/req.h"
#include "mesh/abi/base.h"
#include "mesh/abi/name.h"
#include "mesh/abi/ops.h"
#include "mesh/debug_str.h"
#include "mesh/rs_parse.h"

#include <stdlib.h>
#include <string.h>

void mj_submit_free(mj_submit *s) {
    free(s->program);
    mj_ranges_free(&s->items);
    free(s->setup);
    memset(s, 0, sizeof *s);
}

mj_submit mj_submit_clone(const mj_submit *s) {
    mj_submit c = *s;
    c.program = mstrdup(s->program);
    c.items = mj_ranges_clone(&s->items);
    c.setup = mj_setup_clone(s->setup, s->nsetup);
    return c;
}

void mj_req_free(mj_req *r) {
    if (r->kind == MJ_REQ_SUBMIT) mj_submit_free(&r->submit);
    memset(r, 0, sizeof *r);
}

mj_req mj_req_clone(const mj_req *r) {
    mj_req c = *r;
    if (r->kind == MJ_REQ_SUBMIT) c.submit = mj_submit_clone(&r->submit);
    return c;
}

/* `0x..` hex, else decimal (jobs.rs's `int`). */
static bool int_of(const char *s, size_t n, uint32_t *out) {
    uint64_t v;
    if (n >= 2 && s[0] == '0' && s[1] == 'x') {
        if (!rs_parse_uint(s + 2, n - 2, 16, UINT32_MAX, &v)) return false;
        *out = (uint32_t)v;
        return true;
    }
    return rs_u32(s, n, out);
}

static bool fail(char **err, char *msg) {
    if (err) *err = msg;
    else free(msg);
    return false;
}

static char *bad_kv(const char *k, const char *v) {
    msb b = {0};
    msb_printf(&b, "%s ", k);
    mdebug_str(&b, v, strlen(v));
    msb_puts(&b, "; " MESH_JOBS_SUBMIT_USAGE);
    return msb_take(&b);
}

/* Splits s at commas, calling f on each piece; false at the first piece f refuses. */
static bool each(const char *s, bool (*f)(const char *p, size_t n, void *cx), void *cx) {
    for (;;) {
        const char *c = strchr(s, ',');
        size_t n = c ? (size_t)(c - s) : strlen(s);
        if (!f(s, n, cx)) return false;
        if (!c) return true;
        s = c + 1;
    }
}

static bool name_ok(const char *p, size_t n, void *cx) {
    (void)cx;
    return mesh_valid_name((const uint8_t *)p, n);
}

static bool member_bit(const char *p, size_t n, void *cx) {
    uint32_t k;
    if (!rs_u32(p, n, &k) || k >= MESH_MAX_SYSTEMS) return false;
    *(uint32_t *)cx += (uint32_t)1 << k;
    return true;
}

typedef struct {
    mj_setup *v;
    size_t n;
} setups;

static bool setup_one(const char *p, size_t n, void *cx) {
    setups *s = cx;
    const char *colon = memchr(p, ':', n);
    uint32_t o, a;
    if (!colon || !int_of(p, (size_t)(colon - p), &o) || !int_of(colon + 1, n - (size_t)(colon - p) - 1, &a)) return false;
    s->v = mxrealloc(s->v, (s->n + 1) * sizeof *s->v);
    s->v[s->n++] = (mj_setup){o, a};
    return true;
}

bool mj_submit_parse(const char *const *words, size_t n, mj_submit *out, char **err) {
    if (n == 0) return fail(err, mstrdup(MESH_JOBS_SUBMIT_USAGE));
    const char *p = words[0];
    if (!each(p, name_ok, NULL)) {
        msb b = {0};
        mdebug_str(&b, p, strlen(p));
        msb_puts(&b, ": not a program name (or names joined by commas); " MESH_JOBS_SUBMIT_USAGE);
        return fail(err, msb_take(&b));
    }
    mj_submit s = {.program = mstrdup(p), .op = MESH_OP_JOB, .attempts = 3, .mode = MJ_MODE_COLLECT};
    if ((n - 1) % 2 != 0) {
        mj_submit_free(&s);
        return fail(err, mstrdup(MESH_JOBS_SUBMIT_USAGE));
    }
    bool items = false;
    for (size_t i = 1; i + 1 < n; i += 2) {
        const char *k = words[i], *v = words[i + 1];
        size_t vn = strlen(v);
        bool ok = true;
        if (strcmp(k, "items") == 0) {
            uint64_t num;
            mj_ranges r = {0};
            if (rs_u64(v, vn, &num) && !strchr(v, ',')) r = mj_ranges_of(0, num);
            else if (!mj_ranges_parse(v, &r, err)) {
                mj_submit_free(&s);
                return false;
            }
            mj_ranges_free(&s.items);
            s.items = r;
            items = true;
        } else if (strcmp(k, "op") == 0) ok = int_of(v, vn, &s.op);
        else if (strcmp(k, "priority") == 0) ok = rs_i32(v, vn, &s.priority);
        else if (strcmp(k, "attempts") == 0) {
            uint32_t a;
            ok = rs_u32(v, vn, &a) && a >= 1 && a <= 100;
            if (ok) s.attempts = a;
        } else if (strcmp(k, "deadline") == 0) ok = rs_u64(v, vn, &s.deadline_s);
        else if (strcmp(k, "results") == 0) ok = mj_mode_parse(v, &s.mode);
        else if (strcmp(k, "members") == 0) {
            uint32_t m = 0;
            ok = each(v, member_bit, &m);
            if (ok) s.members = m;
        } else if (strcmp(k, "setup") == 0) {
            setups su = {0};
            ok = each(v, setup_one, &su);
            if (ok) {
                free(s.setup);
                s.setup = su.v;
                s.nsetup = su.n;
            } else free(su.v);
        } else ok = false;
        if (!ok) {
            mj_submit_free(&s);
            return fail(err, bad_kv(k, v));
        }
    }
    uint64_t end;
    char *why = NULL;
    if (!items || mj_ranges_is_empty(&s.items)) why = mstrdup("no items; " MESH_JOBS_SUBMIT_USAGE);
    else if (mj_ranges_end(&s.items, &end) && end == UINT64_MAX && s.mode == MJ_MODE_COLLECT)
        why = mstrdup("an endless job keeps no per-item results: results nonzero or none");
    else if (s.nsetup > 16) why = mstrdup("setup: at most 16 tasks");
    if (why) {
        mj_submit_free(&s);
        return fail(err, why);
    }
    *out = s;
    return true;
}

static bool job_id(const char *s, mj_job_id *id, char **err) {
    if (mj_parse_job(s, id)) return true;
    msb b = {0};
    mdebug_str(&b, s, strlen(s));
    msb_puts(&b, ": a job id is <term>.<n> (`jobs` lists them)");
    return fail(err, msb_take(&b));
}

bool mj_req_parse(const char *const *words, size_t n, mj_req *out, char **err) {
    mj_req r = {0};
    if (n >= 1 && strcmp(words[0], "submit") == 0) {
        r.kind = MJ_REQ_SUBMIT;
        if (!mj_submit_parse(words + 1, n - 1, &r.submit, err)) return false;
    } else if (n == 2 && strcmp(words[0], "cancel") == 0) {
        r.kind = MJ_REQ_CANCEL;
        if (!job_id(words[1], &r.job, err)) return false;
    } else if (n == 2 && strcmp(words[0], "forget") == 0) {
        r.kind = MJ_REQ_FORGET;
        if (!job_id(words[1], &r.job, err)) return false;
    } else if (n == 3 && strcmp(words[0], "tone") == 0) {
        r.kind = MJ_REQ_TONE;
        if (!job_id(words[1], &r.job, err)) return false;
        const char *t = words[2];
        if (strcmp(t, "aces") == 0 || strcmp(t, "0") == 0) r.tone = 0;
        else if (strcmp(t, "reinhard") == 0 || strcmp(t, "1") == 0) r.tone = 1;
        else {
            msb b = {0};
            msb_puts(&b, "tone ");
            mdebug_str(&b, t, strlen(t));
            msb_puts(&b, ": aces or reinhard");
            return fail(err, msb_take(&b));
        }
    } else {
        msb b = {0};
        msb_puts(&b, "not a job change: ");
        for (size_t i = 0; i < n; i++) msb_printf(&b, "%s%s", i ? " " : "", words[i]);
        return fail(err, msb_take(&b));
    }
    *out = r;
    return true;
}
