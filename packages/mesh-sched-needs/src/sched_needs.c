/* sched.rs: Replicas, Migrate, Term, Needs. */
#include "mesh/sched_needs.h"
#include "mesh/abi/backend.h"
#include "mesh/abi/base.h"
#include "mesh/parse.h"
#include "mesh/rust_fmt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *msn_migrate_word(msn_migrate m) { return m == MSN_STATE ? "state" : m == MSN_RESTART ? "restart" : "never"; }

msn_term msn_term_new(msn_term_kind kind, const char *s, uint32_t k) { return (msn_term){kind, s ? mt_strdup(s) : NULL, k}; }

static char *term_bad(const char *s) {
    char *q = rf_debug_str(s);
    char *e = mt_fmt("%s: a backend (%s, %s, %s, %s), \"host:NAME\", \"system:K\" or \"arch:A\"", q, MESH_BACKENDS[0], MESH_BACKENDS[1], MESH_BACKENDS[2],
                     MESH_BACKENDS[3]);
    free(q);
    return e;
}

bool msn_term_parse(const char *s, msn_term *out, char **err) {
    char *a, *b;
    if (mt_split_once(s, ':', &a, &b)) {
        bool ok = true;
        if (strcmp(a, "host") == 0 && *b) *out = msn_term_new(MSN_HOST, b, 0);
        else if (strcmp(a, "system") == 0) {
            uint32_t k;
            if (mp_u32(b, &k) && k < MESH_MAX_SYSTEMS) *out = msn_term_new(MSN_SYSTEM, NULL, k);
            else ok = false;
        } else if (strcmp(a, "arch") == 0 && *b) *out = msn_term_new(MSN_ARCH, b, 0);
        else ok = false;
        free(a);
        free(b);
        if (!ok) *err = term_bad(s);
        return ok;
    }
    for (size_t i = 0; i < MESH_BACKENDS_LEN; i++)
        if (strcmp(s, MESH_BACKENDS[i]) == 0) {
            *out = msn_term_new(MSN_BACKEND, s, 0);
            return true;
        }
    *err = term_bad(s);
    return false;
}

char *msn_term_word(const msn_term *t) {
    switch (t->kind) {
    case MSN_BACKEND: return mt_strdup(t->s);
    case MSN_HOST: return mt_fmt("host:%s", t->s);
    case MSN_SYSTEM: return mt_fmt("system:%u", t->k);
    default: return mt_fmt("arch:%s", t->s);
    }
}

bool msn_term_matches(const msn_term *t, const msm_member *m) {
    switch (t->kind) {
    case MSN_BACKEND: return strcmp(m->gpu, t->s) == 0;
    case MSN_HOST: {
        if (strcmp(m->host, t->s) == 0) return true;
        const char *dot = strchr(m->host, '.');
        size_t n = dot ? (size_t)(dot - m->host) : strlen(m->host);
        return strlen(t->s) == n && memcmp(m->host, t->s, n) == 0;
    }
    case MSN_SYSTEM: return m->id == t->k;
    default: return strcmp(m->arch, t->s) == 0;
    }
}

void msn_terms_push(msn_terms *l, msn_term t) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->v = mt_realloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n++] = t;
}

void msn_terms_free(msn_terms *l) {
    for (size_t i = 0; i < l->n; i++) free(l->v[i].s);
    free(l->v);
    *l = (msn_terms){0};
}

msn_terms msn_terms_clone(const msn_terms *l) {
    msn_terms c = {0};
    for (size_t i = 0; i < l->n; i++) msn_terms_push(&c, msn_term_new(l->v[i].kind, l->v[i].s, l->v[i].k));
    return c;
}

bool msn_terms_eq(const msn_terms *a, const msn_terms *b) {
    if (a->n != b->n) return false;
    for (size_t i = 0; i < a->n; i++)
        if (a->v[i].kind != b->v[i].kind || a->v[i].k != b->v[i].k || !mt_eq(a->v[i].s, b->v[i].s)) return false;
    return true;
}

char *msn_terms_text(const msn_terms *l) {
    if (!l->n) return mt_strdup("-");
    mt_buf b = {0};
    for (size_t i = 0; i < l->n; i++) {
        char *w = msn_term_word(&l->v[i]);
        if (i) mt_putc(&b, ',');
        mt_puts(&b, w);
        free(w);
    }
    return mt_take(&b);
}

msn_needs msn_default(void) {
    msn_needs n = {0};
    mt_push(&n.backends, "cuda");
    n.replicas.all = true;
    n.migrate = MSN_STATE;
    n.warps = 1;
    return n;
}

static char *dup_or_null(const char *s) { return s ? mt_strdup(s) : NULL; }

msn_needs msn_clone(const msn_needs *n) {
    msn_needs c = *n;
    c.backends = mt_strs_clone(&n->backends);
    c.arch = dup_or_null(n->arch);
    c.prefer = msn_terms_clone(&n->prefer);
    c.avoid = msn_terms_clone(&n->avoid);
    c.colocate_with = dup_or_null(n->colocate_with);
    c.feeds = mt_strs_clone(&n->feeds);
    c.group = dup_or_null(n->group);
    c.group_members = msn_terms_clone(&n->group_members);
    return c;
}

bool msn_eq(const msn_needs *a, const msn_needs *b) {
    return mt_strs_eq(&a->backends, &b->backends) && mt_eq(a->arch, b->arch) && a->min_mem_mb == b->min_mem_mb && msn_terms_eq(&a->prefer, &b->prefer) &&
           msn_terms_eq(&a->avoid, &b->avoid) && a->replicas.all == b->replicas.all && (a->replicas.all || a->replicas.n == b->replicas.n) && a->pack == b->pack &&
           mt_eq(a->colocate_with, b->colocate_with) && mt_strs_eq(&a->feeds, &b->feeds) && mt_eq(a->group, b->group) && a->group_size == b->group_size &&
           msn_terms_eq(&a->group_members, &b->group_members) && a->priority == b->priority && a->cpu == b->cpu && a->mem_mb == b->mem_mb &&
           a->migrate == b->migrate && a->lockstep == b->lockstep && a->view_bytes == b->view_bytes && a->warps == b->warps && a->shared == b->shared;
}

void msn_free(msn_needs *n) {
    mt_strs_free(&n->backends);
    free(n->arch);
    msn_terms_free(&n->prefer);
    msn_terms_free(&n->avoid);
    free(n->colocate_with);
    mt_strs_free(&n->feeds);
    free(n->group);
    msn_terms_free(&n->group_members);
    *n = (msn_needs){0};
}

static char *words(const mt_strs *v) { return v->n ? mt_join(v, ",") : mt_strdup("-"); }

char *msn_replicas_word(msn_replicas r) { return r.all ? mt_strdup("all") : mt_fmt("%u", r.n); }

char *msn_encode(const msn_needs *n) {
    char *b = words(&n->backends), *pf = msn_terms_text(&n->prefer), *av = msn_terms_text(&n->avoid), *r = msn_replicas_word(n->replicas),
         *fd = words(&n->feeds), *gm = msn_terms_text(&n->group_members);
    char cpu[RF_FLOAT_MAX];
    char *s = mt_fmt("b=%s;ar=%s;mm=%u;pf=%s;av=%s;r=%s;sp=%s;co=%s;fd=%s;g=%s;gs=%u;gm=%s;pr=%d;cpu=%s;mem=%u;mg=%s;ls=%u;vb=%u;w=%u;sh=%u", b,
                     n->arch ? n->arch : "-", n->min_mem_mb, pf, av, r, n->pack ? "pack" : "spread", n->colocate_with ? n->colocate_with : "-", fd,
                     n->group ? n->group : "-", n->group_size, gm, n->priority, rf_f32(cpu, n->cpu), n->mem_mb, msn_migrate_word(n->migrate),
                     (unsigned)n->lockstep, n->view_bytes, n->warps, n->shared);
    free(b), free(pf), free(av), free(r), free(fd), free(gm);
    return s;
}

static mt_strs parse_words(const char *s) {
    if (strcmp(s, "-") == 0) return (mt_strs){0};
    return mt_split(s, ',');
}

static bool parse_terms(const char *s, msn_terms *out, char **err) {
    msn_terms_free(out);
    if (strcmp(s, "-") == 0) return true;
    mt_strs w = mt_split(s, ',');
    for (size_t i = 0; i < w.n; i++) {
        msn_term t;
        if (!msn_term_parse(w.v[i], &t, err)) {
            mt_strs_free(&w);
            return false;
        }
        msn_terms_push(out, t);
    }
    mt_strs_free(&w);
    return true;
}

static char *bad(const char *k, const char *v) {
    char *q = rf_debug_str(v);
    char *e = mt_fmt("needs: %s=%s", k, q);
    free(q);
    return e;
}

static void set_opt(char **dst, const char *v) { mt_set(dst, strcmp(v, "-") == 0 ? NULL : v); }

static bool field(msn_needs *n, const char *k, const char *v, char **err) {
    if (strcmp(k, "b") == 0) {
        mt_strs_free(&n->backends);
        n->backends = parse_words(v);
    } else if (strcmp(k, "ar") == 0) set_opt(&n->arch, v);
    else if (strcmp(k, "mm") == 0) {
        if (!mp_u32(v, &n->min_mem_mb)) return *err = bad(k, v), false;
    } else if (strcmp(k, "pf") == 0) return parse_terms(v, &n->prefer, err);
    else if (strcmp(k, "av") == 0) return parse_terms(v, &n->avoid, err);
    else if (strcmp(k, "r") == 0) {
        if (strcmp(v, "all") == 0) n->replicas = (msn_replicas){true, 0};
        else {
            uint32_t r;
            if (!mp_u32(v, &r)) return *err = bad(k, v), false;
            n->replicas = (msn_replicas){false, r};
        }
    } else if (strcmp(k, "sp") == 0) n->pack = strcmp(v, "pack") == 0;
    else if (strcmp(k, "co") == 0) set_opt(&n->colocate_with, v);
    else if (strcmp(k, "fd") == 0) {
        mt_strs_free(&n->feeds);
        n->feeds = parse_words(v);
    } else if (strcmp(k, "g") == 0) set_opt(&n->group, v);
    else if (strcmp(k, "gs") == 0) {
        if (!mp_u32(v, &n->group_size)) return *err = bad(k, v), false;
    } else if (strcmp(k, "gm") == 0) return parse_terms(v, &n->group_members, err);
    else if (strcmp(k, "pr") == 0) {
        if (!mp_i32(v, &n->priority)) return *err = bad(k, v), false;
    } else if (strcmp(k, "cpu") == 0) {
        float c;
        if (!mp_f32(v, &c) || !isfinite(c)) return *err = bad(k, v), false;
        n->cpu = c;
    } else if (strcmp(k, "mem") == 0) {
        if (!mp_u32(v, &n->mem_mb)) return *err = bad(k, v), false;
    } else if (strcmp(k, "mg") == 0) {
        if (strcmp(v, "state") == 0) n->migrate = MSN_STATE;
        else if (strcmp(v, "restart") == 0) n->migrate = MSN_RESTART;
        else if (strcmp(v, "never") == 0) n->migrate = MSN_NEVER;
        else return *err = bad(k, v), false;
    } else if (strcmp(k, "ls") == 0) n->lockstep = strcmp(v, "1") == 0;
    else if (strcmp(k, "vb") == 0) {
        if (!mp_u32(v, &n->view_bytes)) return *err = bad(k, v), false;
    } else if (strcmp(k, "w") == 0) {
        uint32_t w;
        if (!mp_u32(v, &w) || w < 1 || w > MESH_MAX_WARPS) return *err = bad(k, v), false;
        n->warps = w;
    } else if (strcmp(k, "sh") == 0) {
        if (!mp_u32(v, &n->shared)) return *err = bad(k, v), false;
    } else {
        char *q = rf_debug_str(k);
        *err = mt_fmt("needs: unknown key %s", q);
        free(q);
        return false;
    }
    return true;
}

bool msn_decode(const char *s, msn_needs *out, char **err) {
    msn_needs n = msn_default();
    mt_strs kvs = mt_split(s, ';');
    bool ok = true;
    for (size_t i = 0; i < kvs.n && ok; i++) {
        char *k, *v;
        if (!mt_split_once(kvs.v[i], '=', &k, &v)) {
            char *q = rf_debug_str(kvs.v[i]);
            *err = mt_fmt("needs: %s", q);
            free(q);
            ok = false;
            break;
        }
        ok = field(&n, k, v, err);
        free(k);
        free(v);
    }
    mt_strs_free(&kvs);
    if (!ok) {
        msn_free(&n);
        return false;
    }
    *out = n;
    return true;
}

char *msn_summary(const msn_needs *n) {
    mt_strs s = {0};
    char *r = msn_replicas_word(n->replicas);
    mt_push_own(&s, mt_fmt("replicas %s", r));
    free(r);
    if (n->pack) mt_push(&s, "pack");
    char *b = mt_join(&n->backends, ",");
    mt_push_own(&s, mt_fmt("backends %s", b));
    free(b);
    if (n->arch) mt_push_own(&s, mt_fmt("arch %s", n->arch));
    if (n->min_mem_mb > 0) mt_push_own(&s, mt_fmt("min_mem_mb %u", n->min_mem_mb));
    if (n->prefer.n) {
        char *t = msn_terms_text(&n->prefer);
        mt_push_own(&s, mt_fmt("prefer %s", t));
        free(t);
    }
    if (n->avoid.n) {
        char *t = msn_terms_text(&n->avoid);
        mt_push_own(&s, mt_fmt("avoid %s", t));
        free(t);
    }
    if (n->colocate_with) mt_push_own(&s, mt_fmt("colocate_with %s", n->colocate_with));
    if (n->group) {
        char *t = msn_terms_text(&n->group_members);
        mt_push_own(&s, mt_fmt("group %s size %u members %s", n->group, n->group_size, t));
        free(t);
    }
    if (n->priority != 0) mt_push_own(&s, mt_fmt("priority %d", n->priority));
    if (n->cpu > 0.0f) mt_push_own(&s, mt_fmt("cpu %.2f", (double)n->cpu));
    if (n->mem_mb > 0) mt_push_own(&s, mt_fmt("mem_mb %u", n->mem_mb));
    mt_push_own(&s, mt_fmt("view %u KiB", n->view_bytes / 1024 + (n->view_bytes % 1024 != 0)));
    if (n->warps > 1 || n->shared > 0) mt_push_own(&s, mt_fmt("node %u warps, %u shared bytes", n->warps, n->shared));
    mt_push_own(&s, mt_fmt("migrate %s", msn_migrate_word(n->migrate)));
    char *out = mt_join(&s, " ");
    mt_strs_free(&s);
    return out;
}
