/* The minimal driver around the executor. */
#include "mesh/exec_wasm_testkit.h"

#include "mesh/abi/base.h"
#include "mesh/abi/control_block.h"
#include "mesh/abi/ext.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/ring.h"
#include "mesh/abi/view_heap.h"
#include "mesh/lease.h"
#include "mesh/manifest.h"

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char *dupf(const char *fmt, const char *a, const char *b) {
    size_t n = strlen(fmt) + strlen(a) + strlen(b) + 1;
    char *s = malloc(n);
    if (s) snprintf(s, n, fmt, a, b);
    return s;
}

bool mesh_kit_open(mesh_kit *kit, const char *cache, char **err) {
    memset(kit, 0, sizeof *kit);
    kit->mb = mesh_mailbox_test();
    snprintf(kit->root, sizeof kit->root, "/tmp/mesh-kit-XXXXXX");
    if (!mkdtemp(kit->root)) {
        if (err) *err = strdup("mkdtemp failed");
        return false;
    }
    kit->heap = MESH_VIEW_HEAP;
    kit->cache = cache ? strdup(cache) : NULL;
    return mesh_wasm_backend_open(kit->mb.base, cache, &kit->b, err);
}

static bool write_file(const char *path, const void *b, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(b, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static bool read_file(const char *path, uint8_t **out, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = malloc(len > 0 ? (size_t)len : 1);
    *n = len > 0 ? (size_t)len : 0;
    bool ok = *out && fread(*out, 1, *n, f) == *n;
    fclose(f);
    return ok;
}

bool mesh_kit_load(mesh_kit *kit, uint32_t slot, const char *manifest, const char *wasm_path, char **err) {
    uint8_t *b;
    size_t n;
    if (!read_file(wasm_path, &b, &n)) {
        if (err) *err = dupf("%s%s: cannot read", wasm_path, "");
        return false;
    }
    bool ok = mesh_kit_load_bytes(kit, slot, manifest, b, n, err);
    free(b);
    return ok;
}

void mesh_kit_reads(mesh_kit *kit, uint32_t slot, uint32_t mask) {
    size_t e = mesh_prog_entry(slot);
    uint32_t g = mesh_mailbox_get(kit->mb, e + MESH_PT_GEN);
    mesh_mailbox_publish(kit->mb, e + MESH_PT_GEN, g + 1);
    mesh_mailbox_set(kit->mb, e + MESH_PT_READS_MASK, mask);
    mesh_mailbox_publish(kit->mb, e + MESH_PT_GEN, g + 2);
}

bool mesh_kit_load_bytes(mesh_kit *kit, uint32_t slot, const char *manifest, const uint8_t *wasm, size_t n, char **err) {
    mesh_manifest m;
    char *why = NULL;
    if (!mesh_manifest_parse(manifest, strlen(manifest), &m, &why)) {
        if (err) *err = why;
        else free(why);
        return false;
    }
    char *dir = dupf("%s/%s", kit->root, m.name);
    mkdir(dir, 0755);
    char *toml = dupf("%s/%s", dir, "program.toml");
    char *img = malloc(strlen(dir) + strlen(m.name) + 8);
    sprintf(img, "%s/%s.wasm", dir, m.name);
    bool ok = write_file(toml, manifest, strlen(manifest)) && write_file(img, wasm, n);
    free(toml);
    free(img);
    free(dir);
    if (!ok) {
        mesh_manifest_free(&m);
        if (err) *err = strdup("cannot write the catalog");
        return false;
    }
    mesh_payload src = {strdup(m.name), strdup(manifest), NULL, 0};
    mesh_prepared prep;
    const mesh_compiler *comp = kit->b->ops->compiler(kit->b);
    if (!comp->ops->prepare(comp, &src, NULL, kit->root, &prep, err)) {
        mesh_manifest_free(&m);
        return false;
    }
    mesh_kit_slot *s = &kit->slot[slot];
    memset(s, 0, sizeof *s);
    if (!kit->b->ops->load(kit->b, &prep, &s->k, err)) {
        mesh_prepared_free(&prep);
        mesh_manifest_free(&m);
        return false;
    }
    s->used = true;
    s->name = strdup(m.name);
    s->nodes = m.nodes ? m.nodes : 1;
    s->view_bytes = m.view_bytes;
    s->view = (kit->heap + 15) & ~(size_t)15;
    kit->heap = s->view + m.view_bytes;
    /* The table entry, as the driver writes it before a launch. */
    mesh_mailbox mb = kit->mb;
    size_t e = mesh_prog_entry(slot);
    mesh_mailbox_zero(mb, e, MESH_PT_BYTES);
    mesh_mailbox_zero(mb, mesh_prog_ctl(slot), MESH_PROG_CTL_BYTES);
    uint8_t name[MESH_NAME_BYTES] = {0};
    memcpy(name, m.name, strlen(m.name) < MESH_NAME_MAX ? strlen(m.name) : MESH_NAME_MAX);
    mesh_mailbox_write(mb, e + MESH_PT_NAME, name, MESH_NAME_BYTES, MESH_NAME_BYTES);
    mesh_mailbox_set(mb, e + MESH_PT_VIEW_OFF, (uint32_t)s->view);
    mesh_mailbox_set(mb, e + MESH_PT_VIEW_BYTES, m.view_bytes);
    mesh_mailbox_set(mb, e + MESH_PT_NODES, s->nodes);
    mesh_mailbox_set(mb, e + MESH_PT_PEER, m.peer);
    mesh_mailbox_set(mb, e + MESH_PT_INPUT_OFF, m.input_off);
    mesh_mailbox_set(mb, e + MESH_PT_INPUT_LEN, m.input_len);
    if (m.has_request) {
        mesh_mailbox_set(mb, e + MESH_PT_REQ_OFF, m.request.req_off);
        mesh_mailbox_set(mb, e + MESH_PT_REQ_MAX, m.request.req_max);
        mesh_mailbox_set(mb, e + MESH_PT_RESP_OFF, m.request.resp_off);
        mesh_mailbox_set(mb, e + MESH_PT_RESP_MAX, m.request.resp_max);
    }
    mesh_mailbox_set(mb, mesh_prog_ext(slot) + MESH_PX_PARAM, m.param);
    mesh_mailbox_set(mb, mesh_prog_ext(slot) + MESH_PX_EXEC, MESH_BACKEND_WASM);
    /* reads = [names]: the slots holding them now ("*": all). */
    uint32_t mask = 0;
    for (size_t i = 0; i < m.nreads; i++) {
        if (strcmp(m.reads[i], "*") == 0) mask = MESH_READS_ALL;
        for (uint32_t t = 0; t < 32; t++)
            if (kit->slot[t].used && kit->slot[t].name && strcmp(kit->slot[t].name, m.reads[i]) == 0) mask |= 1u << t;
    }
    mesh_mailbox_set(mb, e + MESH_PT_READS_MASK, mask);
    mesh_mailbox_publish(mb, e + MESH_PT_STATE, MESH_ST_RUNNING);
    mesh_manifest_free(&m);
    mesh_prepared_free(&prep);
    if (!kit->b->ops->new_stream(kit->b, slot, s->k, &s->st, err)) return false;
    if (!kit->b->ops->launch(kit->b, s->st, s->k, s->nodes, 0, slot, err)) return false;
    uint64_t t0 = mesh_host_ns();
    while (mesh_mailbox_get(mb, mesh_prog_ctl(slot) + MESH_P_ONLINE) < s->nodes) {
        if (mesh_host_ns() - t0 > 2000000000ull) {
            if (err) *err = strdup("the lanes did not come online");
            return false;
        }
        sched_yield();
    }
    return true;
}

static void publish(mesh_kit *kit, uint32_t slot, uint32_t op, uint32_t arg) {
    mesh_kit_slot *s = &kit->slot[slot];
    uint32_t t = s->next++;
    size_t e = mesh_prog_ctl(slot) + MESH_P_RING + MESH_ENTRY_BYTES * (size_t)(t % MESH_PROG_RING);
    mesh_mailbox_set(kit->mb, e + MESH_E_OP, op);
    mesh_mailbox_set(kit->mb, e + MESH_E_ARG, arg);
    mesh_mailbox_set(kit->mb, e + MESH_E_SLOT, t % MESH_PROG_RESULTS);
    mesh_mailbox_publish(kit->mb, e + MESH_E_SEQ, t + 1);
}

static bool wait_mark(mesh_kit *kit, uint32_t slot, uint32_t t, uint32_t *fault, unsigned timeout_ms) {
    size_t mark = mesh_prog_ctl(slot) + MESH_P_MARKS + 4 * (size_t)(t % MESH_PROG_RESULTS);
    uint64_t t0 = mesh_host_ns();
    *fault = 0;
    while (mesh_mailbox_get(kit->mb, mark) != t + 1) {
        uint32_t f = mesh_mailbox_get(kit->mb, mesh_prog_ctl(slot) + MESH_P_FAULT);
        if (f) {
            *fault = f;
            return false;
        }
        if (mesh_host_ns() - t0 > (uint64_t)timeout_ms * 1000000ull) return false;
        sched_yield();
    }
    return true;
}

bool mesh_kit_task(mesh_kit *kit, uint32_t slot, uint32_t op, uint32_t arg, uint32_t *result, uint32_t *fault,
                   unsigned timeout_ms) {
    uint32_t t = kit->slot[slot].next;
    publish(kit, slot, op, arg);
    if (!wait_mark(kit, slot, t, fault, timeout_ms)) return false;
    *result = mesh_mailbox_get(kit->mb, mesh_prog_ctl(slot) + MESH_P_RESULTS + 4 * (size_t)(t % MESH_PROG_RESULTS));
    return true;
}

bool mesh_kit_tasks(mesh_kit *kit, uint32_t slot, uint32_t op, uint32_t arg, uint32_t n, uint32_t *fault, unsigned timeout_ms) {
    mesh_kit_slot *s = &kit->slot[slot];
    uint32_t first = s->next;
    for (uint32_t i = 0; i < n; i++) {
        /* At most a ring of tickets in flight (as the publisher keeps it). */
        if (i >= MESH_PROG_RING && !wait_mark(kit, slot, first + i - MESH_PROG_RING, fault, timeout_ms)) return false;
        publish(kit, slot, op, arg);
    }
    for (uint32_t i = n > MESH_PROG_RING ? n - MESH_PROG_RING : 0; i < n; i++)
        if (!wait_mark(kit, slot, first + i, fault, timeout_ms)) return false;
    return true;
}

void mesh_kit_unload(mesh_kit *kit, uint32_t slot) {
    mesh_kit_slot *s = &kit->slot[slot];
    if (!s->used) return;
    mesh_mailbox_publish(kit->mb, mesh_prog_ctl(slot) + MESH_P_SHUTDOWN, 1);
    bool done = false;
    char *err = NULL;
    while (s->st && kit->b->ops->query(kit->b, s->st, s->k, &done, &err) && !done) sched_yield();
    free(err);
    err = NULL;
    if (s->k) kit->b->ops->unload(kit->b, s->k, &err);
    free(err);
    mesh_stream_drop(s->st);
    size_t e = mesh_prog_entry(slot);
    mesh_mailbox_zero(kit->mb, e, MESH_PT_BYTES);
    mesh_mailbox_zero(kit->mb, mesh_prog_ctl(slot), MESH_PROG_CTL_BYTES);
    free(s->name);
    memset(s, 0, sizeof *s);
}

size_t mesh_kit_view(const mesh_kit *kit, uint32_t slot, size_t off) { return kit->slot[slot].view + off; }

static int remove_one(const char *path, const struct stat *st, int flag, struct FTW *ftw) {
    (void)st, (void)flag, (void)ftw;
    return remove(path);
}

void mesh_kit_close(mesh_kit *kit) {
    for (uint32_t s = 0; s < 32; s++) mesh_kit_unload(kit, s);
    mesh_backend_destroy(kit->b);
    /* The catalog: remove what we wrote. */
    nftw(kit->root, remove_one, 16, FTW_DEPTH | FTW_PHYS);
    free(kit->mb.base);
    free(kit->cache);
}

char *mesh_kit_artifact(const char *id) {
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) return NULL;
    exe[n] = 0;
    char *t = strstr(exe, "/test/");
    if (!t) {
        fprintf(stderr, "mesh-kit: %s is not a sexc test binary\n", exe);
        return NULL;
    }
    *t = 0;
    size_t len = strlen(exe) + strlen(id) + 16;
    char *path = malloc(len);
    snprintf(path, len, "%s/wasm/%s.wasm", exe, id);
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "mesh-kit: %s is not built: run `sexc build` (with this profile) first\n", path);
        free(path);
        return NULL;
    }
    return path;
}
