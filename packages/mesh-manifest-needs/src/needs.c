/* sched.rs's Replicas, Migrate, Term and Needs: the data, parsing, equality and Debug. */
#include "mesh/needs.h"

#include "mesh/abi/backend.h"
#include "mesh/abi/base.h"

#include <stdlib.h>
#include <string.h>

const char *mesh_migrate_word(mesh_migrate m) {
    switch (m) {
    case MESH_MIGRATE_STATE: return "state";
    case MESH_MIGRATE_RESTART: return "restart";
    case MESH_MIGRATE_NEVER: return "never";
    }
    return "state";
}

bool mesh_term_parse(const char *s, mesh_term *out, char **err) {
    *out = (mesh_term){0};
    const char *colon = strchr(s, ':');
    if (colon) {
        size_t kn = (size_t)(colon - s);
        const char *v = colon + 1;
        if (kn == 4 && memcmp(s, "host", 4) == 0 && *v) {
            out->kind = MESH_TERM_HOST;
            out->s = mesh_strdup(v);
            return true;
        }
        if (kn == 6 && memcmp(s, "system", 6) == 0) {
            uint32_t k;
            if (mesh_parse_u32(v, strlen(v), 10, &k) && k < MESH_MAX_SYSTEMS) {
                out->kind = MESH_TERM_SYSTEM;
                out->system = k;
                return true;
            }
        }
        if (kn == 4 && memcmp(s, "arch", 4) == 0 && *v) {
            out->kind = MESH_TERM_ARCH;
            out->s = mesh_strdup(v);
            return true;
        }
    } else {
        for (size_t i = 0; i < MESH_BACKENDS_LEN; i++)
            if (strcmp(s, MESH_BACKENDS[i]) == 0) {
                out->kind = MESH_TERM_BACKEND;
                out->s = mesh_strdup(s);
                return true;
            }
    }
    mesh_text t = {0};
    mesh_text_debug_str(&t, s, strlen(s));
    mesh_text_puts(&t, ": a backend (");
    for (size_t i = 0; i < MESH_BACKENDS_LEN; i++) {
        if (i) mesh_text_puts(&t, ", ");
        mesh_text_puts(&t, MESH_BACKENDS[i]);
    }
    mesh_text_puts(&t, "), \"host:NAME\", \"system:K\" or \"arch:A\"");
    if (err) {
        free(*err);
        *err = mesh_text_take(&t);
    } else {
        mesh_text_free(&t);
    }
    return false;
}

char *mesh_term_word(const mesh_term *t) {
    switch (t->kind) {
    case MESH_TERM_BACKEND: return mesh_strdup(t->s);
    case MESH_TERM_HOST: return mesh_fmt("host:%s", t->s);
    case MESH_TERM_SYSTEM: return mesh_fmt("system:%u", (unsigned)t->system);
    case MESH_TERM_ARCH: return mesh_fmt("arch:%s", t->s);
    }
    return mesh_strdup("");
}

bool mesh_term_eq(const mesh_term *a, const mesh_term *b) {
    if (a->kind != b->kind) return false;
    if (a->kind == MESH_TERM_SYSTEM) return a->system == b->system;
    return strcmp(a->s, b->s) == 0;
}

void mesh_term_copy(mesh_term *dst, const mesh_term *src) {
    *dst = *src;
    dst->s = src->s ? mesh_strdup(src->s) : NULL;
}

void mesh_term_free(mesh_term *t) {
    free(t->s);
    *t = (mesh_term){0};
}

void mesh_needs_default(mesh_needs *n) {
    *n = (mesh_needs){0};
    n->backends = mesh_alloc(sizeof *n->backends);
    n->backends[0] = mesh_strdup("cuda");
    n->nbackends = 1;
    n->replicas.all = true;
    n->migrate = MESH_MIGRATE_STATE;
    n->warps = 1;
}

char **mesh_strs_copy(char *const *v, size_t n) {
    if (n == 0) return NULL;
    char **c = mesh_alloc(n * sizeof *c);
    for (size_t i = 0; i < n; i++) c[i] = mesh_strdup(v[i]);
    return c;
}

void mesh_strs_free(char **v, size_t n) {
    for (size_t i = 0; i < n; i++) free(v[i]);
    free(v);
}

bool mesh_strs_eq(char *const *a, size_t an, char *const *b, size_t bn) {
    if (an != bn) return false;
    for (size_t i = 0; i < an; i++)
        if (strcmp(a[i], b[i]) != 0) return false;
    return true;
}

static mesh_term *terms_copy(const mesh_term *v, size_t n) {
    if (n == 0) return NULL;
    mesh_term *c = mesh_alloc(n * sizeof *c);
    for (size_t i = 0; i < n; i++) mesh_term_copy(&c[i], &v[i]);
    return c;
}

static void terms_free(mesh_term *v, size_t n) {
    for (size_t i = 0; i < n; i++) mesh_term_free(&v[i]);
    free(v);
}

static bool terms_eq(const mesh_term *a, size_t an, const mesh_term *b, size_t bn) {
    if (an != bn) return false;
    for (size_t i = 0; i < an; i++)
        if (!mesh_term_eq(&a[i], &b[i])) return false;
    return true;
}

static bool opt_eq(const char *a, const char *b) { return (!a && !b) || (a && b && strcmp(a, b) == 0); }
static char *opt_copy(const char *s) { return s ? mesh_strdup(s) : NULL; }

void mesh_needs_copy(mesh_needs *dst, const mesh_needs *src) {
    *dst = *src;
    dst->backends = mesh_strs_copy(src->backends, src->nbackends);
    dst->arch = opt_copy(src->arch);
    dst->prefer = terms_copy(src->prefer, src->nprefer);
    dst->avoid = terms_copy(src->avoid, src->navoid);
    dst->colocate_with = opt_copy(src->colocate_with);
    dst->feeds = mesh_strs_copy(src->feeds, src->nfeeds);
    dst->group = opt_copy(src->group);
    dst->group_members = terms_copy(src->group_members, src->ngroup_members);
}

bool mesh_needs_eq(const mesh_needs *a, const mesh_needs *b) {
    return mesh_strs_eq(a->backends, a->nbackends, b->backends, b->nbackends) && opt_eq(a->arch, b->arch) &&
           a->min_mem_mb == b->min_mem_mb && terms_eq(a->prefer, a->nprefer, b->prefer, b->nprefer) &&
           terms_eq(a->avoid, a->navoid, b->avoid, b->navoid) && a->replicas.all == b->replicas.all &&
           (a->replicas.all || a->replicas.n == b->replicas.n) && a->pack == b->pack &&
           opt_eq(a->colocate_with, b->colocate_with) && mesh_strs_eq(a->feeds, a->nfeeds, b->feeds, b->nfeeds) &&
           opt_eq(a->group, b->group) && a->group_size == b->group_size &&
           terms_eq(a->group_members, a->ngroup_members, b->group_members, b->ngroup_members) &&
           a->priority == b->priority && a->cpu == b->cpu && a->mem_mb == b->mem_mb && a->migrate == b->migrate &&
           a->lockstep == b->lockstep && a->view_bytes == b->view_bytes && a->warps == b->warps &&
           a->shared == b->shared;
}

void mesh_needs_free(mesh_needs *n) {
    mesh_strs_free(n->backends, n->nbackends);
    free(n->arch);
    terms_free(n->prefer, n->nprefer);
    terms_free(n->avoid, n->navoid);
    free(n->colocate_with);
    mesh_strs_free(n->feeds, n->nfeeds);
    free(n->group);
    terms_free(n->group_members, n->ngroup_members);
    *n = (mesh_needs){0};
}

void mesh_strs_debug(mesh_text *t, char *const *v, size_t n) {
    mesh_text_putc(t, '[');
    for (size_t i = 0; i < n; i++) {
        if (i) mesh_text_puts(t, ", ");
        mesh_text_debug_str(t, v[i], strlen(v[i]));
    }
    mesh_text_putc(t, ']');
}

static void opt_debug(mesh_text *t, const char *s) {
    if (!s) {
        mesh_text_puts(t, "None");
        return;
    }
    mesh_text_puts(t, "Some(");
    mesh_text_debug_str(t, s, strlen(s));
    mesh_text_putc(t, ')');
}

void mesh_term_debug(mesh_text *t, const mesh_term *x) {
    static const char *const names[] = {"Backend", "Host", "System", "Arch"};
    mesh_text_printf(t, "%s(", names[x->kind]);
    if (x->kind == MESH_TERM_SYSTEM) mesh_text_printf(t, "%u", (unsigned)x->system);
    else mesh_text_debug_str(t, x->s, strlen(x->s));
    mesh_text_putc(t, ')');
}

static void terms_debug(mesh_text *t, const mesh_term *v, size_t n) {
    mesh_text_putc(t, '[');
    for (size_t i = 0; i < n; i++) {
        if (i) mesh_text_puts(t, ", ");
        mesh_term_debug(t, &v[i]);
    }
    mesh_text_putc(t, ']');
}

void mesh_needs_debug(mesh_text *t, const mesh_needs *n) {
    static const char *const migrates[] = {"State", "Restart", "Never"};
    mesh_text_puts(t, "Needs { backends: ");
    mesh_strs_debug(t, n->backends, n->nbackends);
    mesh_text_puts(t, ", arch: ");
    opt_debug(t, n->arch);
    mesh_text_printf(t, ", min_mem_mb: %u, prefer: ", (unsigned)n->min_mem_mb);
    terms_debug(t, n->prefer, n->nprefer);
    mesh_text_puts(t, ", avoid: ");
    terms_debug(t, n->avoid, n->navoid);
    if (n->replicas.all) mesh_text_puts(t, ", replicas: All");
    else mesh_text_printf(t, ", replicas: N(%u)", (unsigned)n->replicas.n);
    mesh_text_printf(t, ", pack: %s, colocate_with: ", n->pack ? "true" : "false");
    opt_debug(t, n->colocate_with);
    mesh_text_puts(t, ", feeds: ");
    mesh_strs_debug(t, n->feeds, n->nfeeds);
    mesh_text_puts(t, ", group: ");
    opt_debug(t, n->group);
    mesh_text_printf(t, ", group_size: %u, group_members: ", (unsigned)n->group_size);
    terms_debug(t, n->group_members, n->ngroup_members);
    mesh_text_printf(t, ", priority: %d, cpu: ", (int)n->priority);
    mesh_text_debug_f32(t, n->cpu);
    mesh_text_printf(t, ", mem_mb: %u, migrate: %s, lockstep: %s, view_bytes: %u, warps: %u, shared: %u }",
                     (unsigned)n->mem_mb, migrates[n->migrate], n->lockstep ? "true" : "false",
                     (unsigned)n->view_bytes, (unsigned)n->warps, (unsigned)n->shared);
}
