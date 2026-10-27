/* The manifest's helpers: Place's parts, the node shape, load from a dir, copy, free, Debug. */
#include "mesh/manifest.h"

#include "mesh/abi/backend.h"
#include "mesh/abi/base.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

size_t mesh_manifest_place_state_bytes(const mesh_manifest_place *p) {
    size_t n = 0;
    for (size_t i = 0; i < p->nstate; i++) n += p->state[i].len;
    return n;
}

uint32_t mesh_manifest_place_out_bytes(const mesh_manifest_place *p) {
    uint32_t n = 0;
    for (size_t i = 0; i < p->nout; i++) n += p->out[i].len;
    return n;
}

void mesh_manifest_place_part(const mesh_manifest_place *p, uint32_t b, mesh_manifest_region *out) {
    for (size_t i = 0; i < p->nout; i++) {
        uint32_t n = p->out[i].len / p->fanout;
        out[i] = (mesh_manifest_region){p->out[i].off + b * n, n};
    }
}

void mesh_manifest_place_part_in_buffer(const mesh_manifest_place *p, uint32_t b, mesh_manifest_region *out) {
    uint32_t at = 0;
    for (size_t i = 0; i < p->nout; i++) {
        uint32_t n = p->out[i].len / p->fanout;
        out[i] = (mesh_manifest_region){at + b * n, n};
        at += p->out[i].len;
    }
}

size_t mesh_manifest_place_part_bytes(const mesh_manifest_place *p) { return mesh_manifest_place_out_bytes(p) / p->fanout; }

uint32_t mesh_manifest_place_mask(uint32_t lo, uint32_t hi) {
    uint32_t m = 0;
    for (uint32_t b = lo; b < hi; b++) m |= 1u << b;
    return m;
}

uint32_t mesh_manifest_threads(const mesh_manifest *m) { return MESH_NODE_THREADS * m->warps; }
uint32_t mesh_manifest_smem(const mesh_manifest *m) { return MESH_RT_SHARED_BYTES + m->shared_bytes; }

bool mesh_manifest_native(const mesh_manifest *m) {
    for (size_t i = 0; i < m->nbackends; i++)
        if (strcmp(m->backends[i], MESH_BACKENDS[MESH_BACKEND_NATIVE]) == 0) return true;
    return false;
}

static void place_free(mesh_manifest_place *p) {
    if (!p) return;
    free(p->state);
    free(p->out);
    free(p);
}

void mesh_manifest_free(mesh_manifest *m) {
    free(m->name);
    mesh_strs_free(m->reads, m->nreads);
    for (size_t i = 0; i < MESH_COUNTERS; i++) free(m->counters[i]);
    for (size_t i = 0; i < m->nfields; i++) free(m->fields[i].name);
    free(m->fields);
    place_free(m->place);
    mesh_strs_free(m->backends, m->nbackends);
    if (m->inbox) free(m->inbox->ty);
    free(m->inbox);
    for (size_t i = 0; i < m->nfeeds; i++) {
        free(m->feeds[i].consumer);
        free(m->feeds[i].ty);
    }
    free(m->feeds);
    mesh_needs_free(&m->needs);
    *m = (mesh_manifest){0};
}

static void *dup_bytes(const void *p, size_t n) {
    if (!p || n == 0) return NULL;
    void *d = mesh_alloc(n);
    memcpy(d, p, n);
    return d;
}

void mesh_manifest_copy(mesh_manifest *dst, const mesh_manifest *src) {
    *dst = *src;
    dst->name = mesh_strdup(src->name);
    dst->reads = mesh_strs_copy(src->reads, src->nreads);
    for (size_t i = 0; i < MESH_COUNTERS; i++) dst->counters[i] = mesh_strdup(src->counters[i] ? src->counters[i] : "");
    dst->fields = dup_bytes(src->fields, src->nfields * sizeof *src->fields);
    for (size_t i = 0; i < src->nfields; i++) dst->fields[i].name = mesh_strdup(src->fields[i].name);
    if (src->place) {
        dst->place = dup_bytes(src->place, sizeof *src->place);
        dst->place->state = dup_bytes(src->place->state, src->place->nstate * sizeof(mesh_manifest_region));
        dst->place->out = dup_bytes(src->place->out, src->place->nout * sizeof(mesh_manifest_region));
    }
    dst->backends = mesh_strs_copy(src->backends, src->nbackends);
    if (src->inbox) {
        dst->inbox = dup_bytes(src->inbox, sizeof *src->inbox);
        dst->inbox->ty = mesh_strdup(src->inbox->ty);
    }
    dst->feeds = dup_bytes(src->feeds, src->nfeeds * sizeof *src->feeds);
    for (size_t i = 0; i < src->nfeeds; i++) {
        dst->feeds[i].consumer = mesh_strdup(src->feeds[i].consumer);
        dst->feeds[i].ty = mesh_strdup(src->feeds[i].ty);
    }
    mesh_needs_copy(&dst->needs, &src->needs);
}

/* Reads a whole file; NULL with errno on failure. */
static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    mesh_text t = {0};
    char buf[8192];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) mesh_text_putn(&t, buf, k);
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        mesh_text_free(&t);
        errno = EIO;
        return NULL;
    }
    *n = t.len;
    return mesh_text_take(&t);
}

/* Rust's io::Error Display: "No such file or directory (os error 2)". */
static char *os_error(int e) { return mesh_fmt("%s (os error %d)", strerror(e), e); }

bool mesh_manifest_load(const char *path, mesh_manifest *out, char **ptx, char **err) {
    struct stat st;
    bool dir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    char *toml = dir ? mesh_fmt("%s/program.toml", path) : mesh_strdup(path);
    size_t n;
    char *text = slurp(toml, &n);
    if (!text) {
        char *e = os_error(errno);
        mesh_errf(err, "%s: %s", toml, e);
        free(e);
        free(toml);
        return false;
    }
    char *why = NULL;
    bool ok = mesh_manifest_parse(text, n, out, &why);
    free(text);
    if (!ok) {
        mesh_errf(err, "%s: %s", toml, why);
        free(why);
        free(toml);
        return false;
    }
    /* <name>.ptx beside the manifest. */
    char *slash = strrchr(toml, '/');
    if (ptx) *ptx = slash ? mesh_fmt("%.*s/%s.ptx", (int)(slash - toml), toml, out->name) : mesh_fmt("%s.ptx", out->name);
    free(toml);
    return true;
}

static void dstr(mesh_text *t, const char *s) { mesh_text_debug_str(t, s, strlen(s)); }
static void dbool(mesh_text *t, bool b) { mesh_text_puts(t, b ? "true" : "false"); }

static void regions_debug(mesh_text *t, const mesh_manifest_region *r, size_t n) {
    mesh_text_putc(t, '[');
    for (size_t i = 0; i < n; i++) mesh_text_printf(t, "%s(%u, %u)", i ? ", " : "", (unsigned)r[i].off, (unsigned)r[i].len);
    mesh_text_putc(t, ']');
}

void mesh_manifest_debug(mesh_text *t, const mesh_manifest *m) {
    mesh_text_puts(t, "Manifest { name: ");
    dstr(t, m->name);
    mesh_text_printf(t, ", abi: %u, view_bytes: %u, view_abi: %u, snap_bytes: %u, nodes: %u, nodes_fit: ", (unsigned)m->abi,
                     (unsigned)m->view_bytes, (unsigned)m->view_abi, (unsigned)m->snap_bytes, (unsigned)m->nodes);
    dbool(t, m->nodes_fit);
    mesh_text_printf(t, ", warps: %u, shared_bytes: %u, scratch_bytes: %u, sms: %u, policy: %u, hz: %u, fanout: %u, batch: ",
                     (unsigned)m->warps, (unsigned)m->shared_bytes, (unsigned)m->scratch_bytes, (unsigned)m->sms,
                     (unsigned)m->policy, (unsigned)m->hz, (unsigned)m->fanout);
    dbool(t, m->batch);
    mesh_text_printf(t, ", arg_mode: %u, arg_off: %u, reads: ", (unsigned)m->arg_mode, (unsigned)m->arg_off);
    mesh_strs_debug(t, m->reads, m->nreads);
    mesh_text_printf(t, ", input_off: %u, input_len: %u, request: ", (unsigned)m->input_off, (unsigned)m->input_len);
    if (m->has_request)
        mesh_text_printf(t, "Some(Window { req_off: %u, req_max: %u, resp_off: %u, resp_max: %u })", (unsigned)m->request.req_off,
                         (unsigned)m->request.req_max, (unsigned)m->request.resp_off, (unsigned)m->request.resp_max);
    else
        mesh_text_puts(t, "None");
    mesh_text_printf(t, ", peer: %u, counters: ", (unsigned)m->peer);
    mesh_strs_debug(t, m->counters, MESH_COUNTERS);
    mesh_text_puts(t, ", fields: [");
    for (size_t i = 0; i < m->nfields; i++) {
        mesh_text_puts(t, i ? ", Field { name: " : "Field { name: ");
        dstr(t, m->fields[i].name);
        mesh_text_printf(t, ", ty: %u, off: %u }", (unsigned)m->fields[i].ty, (unsigned)m->fields[i].off);
    }
    mesh_text_puts(t, "], autostart: ");
    dbool(t, m->autostart);
    mesh_text_puts(t, ", place: ");
    if (m->place) {
        mesh_text_printf(t, "Some(Place { cap: %u, state: ", (unsigned)m->place->cap);
        regions_debug(t, m->place->state, m->place->nstate);
        mesh_text_printf(t, ", frame: %u, out: ", (unsigned)m->place->frame);
        regions_debug(t, m->place->out, m->place->nout);
        mesh_text_printf(t, ", show: %u, fanout: %u })", (unsigned)m->place->show, (unsigned)m->place->fanout);
    } else {
        mesh_text_puts(t, "None");
    }
    mesh_text_puts(t, ", backends: ");
    mesh_strs_debug(t, m->backends, m->nbackends);
    mesh_text_printf(t, ", param: %u, ftz: ", (unsigned)m->param);
    dbool(t, m->ftz);
    mesh_text_printf(t, ", mem_mb: %u, task_ms: %u, cpu_pct: %u, mem: %llu, nice: %u, background: ", (unsigned)m->mem_mb,
                     (unsigned)m->task_ms, (unsigned)m->cpu_pct, (unsigned long long)m->mem, (unsigned)m->nice);
    dbool(t, m->background);
    mesh_text_puts(t, ", device_gpu: ");
    dbool(t, m->device_gpu);
    mesh_text_puts(t, ", inbox: ");
    if (m->inbox) {
        mesh_text_printf(t, "Some(Inbox { off: %u, cap: %u, rec: %u, ty: ", (unsigned)m->inbox->off, (unsigned)m->inbox->cap,
                         (unsigned)m->inbox->rec);
        dstr(t, m->inbox->ty);
        mesh_text_puts(t, " })");
    } else {
        mesh_text_puts(t, "None");
    }
    mesh_text_puts(t, ", feeds: [");
    for (size_t i = 0; i < m->nfeeds; i++) {
        mesh_text_puts(t, i ? ", (" : "(");
        dstr(t, m->feeds[i].consumer);
        mesh_text_puts(t, ", ");
        dstr(t, m->feeds[i].ty);
        mesh_text_putc(t, ')');
    }
    mesh_text_puts(t, "], needs: ");
    mesh_needs_debug(t, &m->needs);
    mesh_text_puts(t, ", job_base: ");
    if (m->has_job_base) mesh_text_printf(t, "Some(%u)", (unsigned)m->job_base);
    else mesh_text_puts(t, "None");
    mesh_text_puts(t, ", job_out: ");
    if (m->has_job_out) mesh_text_printf(t, "Some((%u, %u))", (unsigned)m->job_out_off, (unsigned)m->job_out_rec);
    else mesh_text_puts(t, "None");
    mesh_text_puts(t, ", job_payload: ");
    if (m->has_job_payload) mesh_text_printf(t, "Some((%u, %u))", (unsigned)m->job_payload_off, (unsigned)m->job_payload_len);
    else mesh_text_puts(t, "None");
    mesh_text_puts(t, " }");
}
