/* sched.rs: Replicas, Migrate, Term, Needs. */
#include "mesh/sched_needs.h"
#include "mesh/abi/backend.h"
#include "mesh/abi/base.h"
#include "mesh/parse.h"
#include "mesh/rust_fmt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

mesh_term msn_term_new(mesh_term_kind kind, const char *s, uint32_t system) { return (mesh_term){kind, s ? mt_strdup(s) : NULL, system}; }

bool msn_term_matches(const mesh_term *t, const msm_member *m) {
    switch (t->kind) {
    case MESH_TERM_BACKEND: return strcmp(m->gpu, t->s) == 0;
    case MESH_TERM_HOST: {
        if (strcmp(m->host, t->s) == 0) return true;
        const char *dot = strchr(m->host, '.');
        size_t n = dot ? (size_t)(dot - m->host) : strlen(m->host);
        return strlen(t->s) == n && memcmp(m->host, t->s, n) == 0;
    }
    case MESH_TERM_SYSTEM: return m->id == t->system;
    default: return strcmp(m->arch, t->s) == 0;
    }
}

void msn_terms_push(mesh_term **v, size_t *n, mesh_term t) {
    *v = mt_realloc(*v, (*n + 1) * sizeof **v);
    (*v)[(*n)++] = t;
}

void msn_strs_push(char ***v, size_t *n, const char *s) {
    *v = mt_realloc(*v, (*n + 1) * sizeof **v);
    (*v)[(*n)++] = mt_strdup(s);
}

char *msn_terms_text(const mesh_term *v, size_t n) {
    if (!n) return mt_strdup("-");
    mt_buf b = {0};
    for (size_t i = 0; i < n; i++) {
        char *w = mesh_term_word(&v[i]);
        if (i) mt_putc(&b, ',');
        mt_puts(&b, w);
        free(w);
    }
    return mt_take(&b);
}

mesh_needs msn_default(void) {
    mesh_needs n;
    mesh_needs_default(&n);
    return n;
}

mesh_needs msn_clone(const mesh_needs *n) {
    mesh_needs c;
    mesh_needs_copy(&c, n);
    return c;
}

char *msn_strs_join(char *const *v, size_t n, const char *sep) {
    mt_buf b = {0};
    for (size_t i = 0; i < n; i++) {
        if (i) mt_puts(&b, sep);
        mt_puts(&b, v[i]);
    }
    return n ? mt_take(&b) : mt_strdup("");
}

bool msn_strs_has(char *const *v, size_t n, const char *s) {
    for (size_t i = 0; i < n; i++)
        if (strcmp(v[i], s) == 0) return true;
    return false;
}

static char *join(char *const *v, size_t n, const char *empty) { return n ? msn_strs_join(v, n, ",") : mt_strdup(empty); }

char *msn_replicas_word(mesh_replicas r) { return r.all ? mt_strdup("all") : mt_fmt("%u", r.n); }

char *msn_encode(const mesh_needs *n) {
    char *b = join(n->backends, n->nbackends, "-"), *pf = msn_terms_text(n->prefer, n->nprefer), *av = msn_terms_text(n->avoid, n->navoid), *r = msn_replicas_word(n->replicas),
         *fd = join(n->feeds, n->nfeeds, "-"), *gm = msn_terms_text(n->group_members, n->ngroup_members);
    char cpu[RF_FLOAT_MAX];
    char *s = mt_fmt("b=%s;ar=%s;mm=%u;pf=%s;av=%s;r=%s;sp=%s;co=%s;fd=%s;g=%s;gs=%u;gm=%s;pr=%d;cpu=%s;mem=%u;mg=%s;ls=%u;vb=%u;w=%u;sh=%u", b,
                     n->arch ? n->arch : "-", n->min_mem_mb, pf, av, r, n->pack ? "pack" : "spread", n->colocate_with ? n->colocate_with : "-", fd,
                     n->group ? n->group : "-", n->group_size, gm, n->priority, rf_f32(cpu, n->cpu), n->mem_mb, mesh_migrate_word(n->migrate),
                     (unsigned)n->lockstep, n->view_bytes, n->warps, n->shared);
    free(b), free(pf), free(av), free(r), free(fd), free(gm);
    return s;
}

static void parse_words(const char *s, char ***v, size_t *n) {
    mesh_strs_free(*v, *n);
    *v = NULL, *n = 0;
    if (strcmp(s, "-") == 0) return;
    mt_strs w = mt_split(s, ',');
    for (size_t i = 0; i < w.n; i++) msn_strs_push(v, n, w.v[i]);
    mt_strs_free(&w);
}

static bool parse_terms(const char *s, mesh_term **v, size_t *n, char **err) {
    for (size_t i = 0; i < *n; i++) mesh_term_free(&(*v)[i]);
    free(*v);
    *v = NULL, *n = 0;
    if (strcmp(s, "-") == 0) return true;
    mt_strs w = mt_split(s, ',');
    for (size_t i = 0; i < w.n; i++) {
        mesh_term t;
        char *e = NULL;
        if (!mesh_term_parse(w.v[i], &t, &e)) {
            *err = e;
            mt_strs_free(&w);
            return false;
        }
        msn_terms_push(v, n, t);
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

static bool field(mesh_needs *n, const char *k, const char *v, char **err) {
    if (strcmp(k, "b") == 0) {
        parse_words(v, &n->backends, &n->nbackends);
    } else if (strcmp(k, "ar") == 0) set_opt(&n->arch, v);
    else if (strcmp(k, "mm") == 0) {
        if (!mp_u32(v, &n->min_mem_mb)) return *err = bad(k, v), false;
    } else if (strcmp(k, "pf") == 0) return parse_terms(v, &n->prefer, &n->nprefer, err);
    else if (strcmp(k, "av") == 0) return parse_terms(v, &n->avoid, &n->navoid, err);
    else if (strcmp(k, "r") == 0) {
        if (strcmp(v, "all") == 0) n->replicas = (mesh_replicas){true, 0};
        else {
            uint32_t r;
            if (!mp_u32(v, &r)) return *err = bad(k, v), false;
            n->replicas = (mesh_replicas){false, r};
        }
    } else if (strcmp(k, "sp") == 0) n->pack = strcmp(v, "pack") == 0;
    else if (strcmp(k, "co") == 0) set_opt(&n->colocate_with, v);
    else if (strcmp(k, "fd") == 0) {
        parse_words(v, &n->feeds, &n->nfeeds);
    } else if (strcmp(k, "g") == 0) set_opt(&n->group, v);
    else if (strcmp(k, "gs") == 0) {
        if (!mp_u32(v, &n->group_size)) return *err = bad(k, v), false;
    } else if (strcmp(k, "gm") == 0) return parse_terms(v, &n->group_members, &n->ngroup_members, err);
    else if (strcmp(k, "pr") == 0) {
        if (!mp_i32(v, &n->priority)) return *err = bad(k, v), false;
    } else if (strcmp(k, "cpu") == 0) {
        float c;
        if (!mp_f32(v, &c) || !isfinite(c)) return *err = bad(k, v), false;
        n->cpu = c;
    } else if (strcmp(k, "mem") == 0) {
        if (!mp_u32(v, &n->mem_mb)) return *err = bad(k, v), false;
    } else if (strcmp(k, "mg") == 0) {
        if (strcmp(v, "state") == 0) n->migrate = MESH_MIGRATE_STATE;
        else if (strcmp(v, "restart") == 0) n->migrate = MESH_MIGRATE_RESTART;
        else if (strcmp(v, "never") == 0) n->migrate = MESH_MIGRATE_NEVER;
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

bool msn_decode(const char *s, mesh_needs *out, char **err) {
    mesh_needs n = msn_default();
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
        mesh_needs_free(&n);
        return false;
    }
    *out = n;
    return true;
}

char *msn_summary(const mesh_needs *n) {
    mt_strs s = {0};
    char *r = msn_replicas_word(n->replicas);
    mt_push_own(&s, mt_fmt("replicas %s", r));
    free(r);
    if (n->pack) mt_push(&s, "pack");
    char *b = join(n->backends, n->nbackends, "");
    mt_push_own(&s, mt_fmt("backends %s", b));
    free(b);
    if (n->arch) mt_push_own(&s, mt_fmt("arch %s", n->arch));
    if (n->min_mem_mb > 0) mt_push_own(&s, mt_fmt("min_mem_mb %u", n->min_mem_mb));
    if (n->nprefer) {
        char *t = msn_terms_text(n->prefer, n->nprefer);
        mt_push_own(&s, mt_fmt("prefer %s", t));
        free(t);
    }
    if (n->navoid) {
        char *t = msn_terms_text(n->avoid, n->navoid);
        mt_push_own(&s, mt_fmt("avoid %s", t));
        free(t);
    }
    if (n->colocate_with) mt_push_own(&s, mt_fmt("colocate_with %s", n->colocate_with));
    if (n->group) {
        char *t = msn_terms_text(n->group_members, n->ngroup_members);
        mt_push_own(&s, mt_fmt("group %s size %u members %s", n->group, n->group_size, t));
        free(t);
    }
    if (n->priority != 0) mt_push_own(&s, mt_fmt("priority %d", n->priority));
    if (n->cpu > 0.0f) mt_push_own(&s, mt_fmt("cpu %.2f", (double)n->cpu));
    if (n->mem_mb > 0) mt_push_own(&s, mt_fmt("mem_mb %u", n->mem_mb));
    mt_push_own(&s, mt_fmt("view %u KiB", n->view_bytes / 1024 + (n->view_bytes % 1024 != 0)));
    if (n->warps > 1 || n->shared > 0) mt_push_own(&s, mt_fmt("node %u warps, %u shared bytes", n->warps, n->shared));
    mt_push_own(&s, mt_fmt("migrate %s", mesh_migrate_word(n->migrate)));
    char *out = mt_join(&s, " ");
    mt_strs_free(&s);
    return out;
}
