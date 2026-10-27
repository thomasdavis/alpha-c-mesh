/* backend.rs's free functions and the trait defaults, over the C tables. */
#include "mesh/backend.h"

#include "mesh/abi/base.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

uint32_t mesh_caps_word(const mesh_caps *c) { return mesh_backend_word(c->kind, c->family); }
const char *mesh_caps_backend_name(const mesh_caps *c) { return MESH_BACKENDS[c->kind]; }

void mesh_caps_copy(mesh_caps *dst, const mesh_caps *src) {
    *dst = *src;
    dst->name = mesh_strdup(src->name ? src->name : "");
    dst->arch = mesh_strdup(src->arch ? src->arch : "");
}

void mesh_caps_free(mesh_caps *c) {
    free(c->name);
    free(c->arch);
    *c = (mesh_caps){0};
}

char *mesh_backend_label(uint32_t word) {
    uint32_t k = mesh_backend_kind(word), f = mesh_backend_family(word);
    if (k == MESH_BACKEND_CUDA) return mesh_strdup("cuda");
    if (k == MESH_BACKEND_WASM) return mesh_strdup("wasm");
    if (k == MESH_BACKEND_METAL) return f > 0 ? mesh_fmt("metal-apple%u", (unsigned)f) : mesh_strdup("metal");
    return mesh_fmt("backend%u", (unsigned)k);
}

bool mesh_kernel_regs(const mesh_kernel *k, uint32_t *out, char **err) { return k->ops->regs(k, out, err); }
bool mesh_kernel_local_bytes(const mesh_kernel *k, uint32_t *out, char **err) { return k->ops->local_bytes(k, out, err); }
bool mesh_kernel_max_blocks_per_sm(const mesh_kernel *k, uint32_t *out, char **err) {
    return k->ops->max_blocks_per_sm(k, out, err);
}
uint64_t mesh_kernel_scratch(const mesh_kernel *k) { return k->ops->scratch(k); }
bool mesh_kernel_global_u32(const mesh_kernel *k, const char *name, uint32_t *out, char **err) {
    return k->ops->global_u32(k, name, out, err);
}

void mesh_kernel_drop(mesh_kernel *k) {
    if (!k) return;
    if (k->ops->drop) k->ops->drop(k);
    free(k);
}

void mesh_stream_drop(mesh_stream *s) {
    if (!s) return;
    if (s->drop) s->drop(s);
    free(s);
}

void mesh_prepared_free(mesh_prepared *p) {
    mesh_manifest_free(&p->manifest);
    free(p->code);
    mesh_payload_free(&p->src);
    mesh_kernel_drop(p->kernel);
    *p = (mesh_prepared){0};
}

char *mesh_backend_crashed(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k) {
    return b->ops->crashed ? b->ops->crashed(b, st, k) : NULL;
}
bool mesh_backend_owns_halt(const mesh_backend *b) { return b->ops->owns_halt && b->ops->owns_halt(b); }
bool mesh_backend_reads_live(const mesh_backend *b) { return b->ops->reads_live && b->ops->reads_live(b); }
void mesh_backend_sample(const mesh_backend *b) {
    if (b->ops->sample) b->ops->sample(b);
}
uint32_t mesh_backend_online_timeout_ms(const mesh_backend *b) {
    return b->ops->online_timeout_ms ? b->ops->online_timeout_ms(b) : 2000;
}
void mesh_backend_destroy(mesh_backend *b) {
    if (b && b->ops->destroy) b->ops->destroy(b);
}

static void join_backends(mesh_text *t, char *const *v, size_t n) {
    for (size_t i = 0; i < n; i++) mesh_text_printf(t, "%s%s", i ? ", " : "", v[i]);
}

bool mesh_pick_exec(const mesh_manifest *m, const uint32_t *have, size_t nhave, uint32_t *exec, char **err) {
    for (size_t i = 0; i < m->nbackends; i++)
        for (size_t j = 0; j < nhave; j++)
            if (strcmp(MESH_BACKENDS[have[j]], m->backends[i]) == 0) {
                *exec = have[j];
                return true;
            }
    mesh_text t = {0};
    mesh_text_printf(&t, "unsupported: %s runs on ", m->name);
    join_backends(&t, m->backends, m->nbackends);
    mesh_text_puts(&t, " (manifest `backends`); this meshd runs ");
    for (size_t j = 0; j < nhave; j++) mesh_text_printf(&t, "%s%s", j ? ", " : "", MESH_BACKENDS[have[j]]);
    if (err) {
        free(*err);
        *err = mesh_text_take(&t);
    } else {
        mesh_text_free(&t);
    }
    return false;
}

const char *mesh_native_target(void) {
#if defined(__APPLE__) && defined(__aarch64__)
    return "aarch64-apple-darwin";
#elif defined(__APPLE__)
    return "x86_64-apple-darwin";
#elif defined(__aarch64__)
    return "aarch64-unknown-linux-gnu";
#elif defined(__x86_64__)
    return "x86_64-unknown-linux-gnu";
#else
    return "unknown";
#endif
}

char *mesh_native_binary(const char *dir, const char *name) { return mesh_fmt("%s/%s/%s", dir, mesh_native_target(), name); }

bool mesh_runs_here(const char *dir, const mesh_manifest *m, const uint32_t *have, size_t nhave) {
    uint32_t exec;
    if (!mesh_pick_exec(m, have, nhave, &exec, NULL)) return false;
    if (exec != MESH_BACKEND_NATIVE) return true;
    char *bin = mesh_native_binary(dir, m->name);
    struct stat st;
    bool file = stat(bin, &st) == 0 && S_ISREG(st.st_mode);
    free(bin);
    return file;
}

bool mesh_check_manifest(const mesh_payload *src, uint32_t kind, mesh_manifest *out, char **err) {
    char *why = NULL;
    if (!mesh_manifest_parse(src->manifest, strlen(src->manifest), out, &why)) {
        mesh_errf(err, "manifest: %s: %s", src->dir, why);
        free(why);
        return false;
    }
    if (out->abi != MESH_RT_ABI) {
        mesh_errf(err, "abi: %s declares abi %u, meshd runs %u", out->name, (unsigned)out->abi, (unsigned)MESH_RT_ABI);
        goto fail;
    }
    const char *me = MESH_BACKENDS[kind];
    bool listed = false;
    for (size_t i = 0; i < out->nbackends; i++) listed = listed || strcmp(out->backends[i], me) == 0;
    if (!listed) {
        mesh_text t = {0};
        mesh_text_printf(&t, "unsupported: %s runs on ", out->name);
        join_backends(&t, out->backends, out->nbackends);
        mesh_text_printf(&t, " (manifest `backends`); this meshd runs %s", me);
        mesh_errf(err, "%s", mesh_text_str(&t));
        mesh_text_free(&t);
        goto fail;
    }
    if (out->sms != 0) {
        mesh_errf(err, "manifest: %s: sms = %u: per-program green partitions are not implemented", out->name, (unsigned)out->sms);
        goto fail;
    }
    return true;
fail:
    mesh_manifest_free(out);
    return false;
}

bool mesh_prepare_any(const mesh_compiler_entry *comps, size_t n, mesh_payload *src, const uint64_t *expect, const char *root,
                      mesh_prepared *out, char **err) {
    mesh_manifest m;
    char *why = NULL;
    if (!mesh_manifest_parse(src->manifest, strlen(src->manifest), &m, &why)) {
        mesh_errf(err, "manifest: %s: %s", src->dir, why);
        free(why);
        mesh_payload_free(src);
        return false;
    }
    uint32_t *kinds = mesh_alloc((n ? n : 1) * sizeof *kinds);
    for (size_t i = 0; i < n; i++) kinds[i] = comps[i].kind;
    uint32_t exec;
    bool ok = mesh_pick_exec(&m, kinds, n, &exec, err);
    free(kinds);
    if (!ok) {
        mesh_manifest_free(&m);
        mesh_payload_free(src);
        return false;
    }
    const mesh_compiler *comp = NULL;
    for (size_t i = 0; i < n && !comp; i++)
        if (comps[i].kind == exec) comp = comps[i].comp;
    if (comp->ops->needs_ptx(comp) && src->ptx_len == 0) {
        mesh_errf(err, "manifest: %s/%s.ptx (%s): no PTX in the catalog (the %s image)", src->dir, m.name, m.name, MESH_BACKENDS[exec]);
        mesh_manifest_free(&m);
        mesh_payload_free(src);
        return false;
    }
    mesh_manifest_free(&m);
    return comp->ops->prepare(comp, src, expect, root, out, err);
}

/* The registered exit cleanups, by order. */
static struct {
    int order;
    void (*fn)(void);
} cleanups[16];
static size_t ncleanups;

void mesh_backend_on_cleanup(int order, void (*fn)(void)) {
    for (size_t i = 0; i < ncleanups; i++)
        if (cleanups[i].fn == fn) return;
    if (ncleanups == sizeof cleanups / sizeof cleanups[0]) return;
    size_t at = ncleanups++;
    while (at > 0 && cleanups[at - 1].order > order) {
        cleanups[at] = cleanups[at - 1];
        at--;
    }
    cleanups[at].order = order;
    cleanups[at].fn = fn;
}

void mesh_backend_cleanup(void) {
    for (size_t i = 0; i < ncleanups; i++) cleanups[i].fn();
}
