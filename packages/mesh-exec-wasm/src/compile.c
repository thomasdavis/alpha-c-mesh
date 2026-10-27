/* Images (mesh-host/src/wasm.rs `Program`, `instantiate`, `WasmCompiler::prepare`). */
#include "mesh/exec_wasm.h"

#include "mesh/abi/backend.h"
#include "mesh/abi/base.h"
#include "mesh/abi/fnv.h"
#include "mesh/abi/wasm.h"
#include "mesh/log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "exec_internal.h"
#include "rt_internal.h"

wasm_instance *mesh_wasm_instantiate(const mesh_wasm_program *p, mesh_mailbox mb, uint32_t slot, uint32_t node,
                                     uint32_t nodes, mesh_wasm_lane_data *data, char **err) {
    mesh_wasm_lane_data_init(data, mb, slot, node, nodes, p->task_ms, &p->rt->epoch);
    size_t n;
    const wasm_host_func *imports = mesh_wasm_imports(&n);
    /* StoreLimits: memory at mem_bytes, one instance, 4 tables, 1 memory. */
    wasm_store_limits lim = {p->mem_bytes, 4, 1, 0, 0};
    char msg[512];
    wasm_instance *inst = wasm_instantiate(p->module, imports, n, &lim, data, msg, sizeof msg);
    if (!inst) {
        if (err) {
            bool link = strncmp(msg, "unknown import", 14) == 0 || strncmp(msg, "incompatible import", 19) == 0;
            *err = link ? mesh_wasm_fmt("load: %s: %s", p->name, msg) : mesh_wasm_fmt("load: %s: instantiate: %s", p->name, msg);
        }
        return NULL;
    }
    /* No deadline until a task. */
    wasm_set_epoch(inst, &p->rt->epoch);
    wasm_set_deadline(inst, UINT64_MAX / 2);
    return inst;
}

bool mesh_wasm_entry_points(const mesh_wasm_program *p, wasm_instance *inst, uint32_t *init, uint32_t *task, char **err) {
    char msg[256];
    if (!wasm_module_export(wasm_compiled_module(p->module), MESH_WASM_EXPORT_MEMORY, WASM_EXTERN_MEMORY) ||
        !wasm_memory(inst, NULL)) {
        if (err) *err = mesh_wasm_fmt("load: %s: no `%s` export", p->name, MESH_WASM_EXPORT_MEMORY);
        return false;
    }
    int64_t f = wasm_instance_func(inst, MESH_WASM_EXPORT_INIT, ">", msg, sizeof msg);
    if (f < 0) {
        if (err) *err = mesh_wasm_fmt("load: %s: %s: %s", p->name, MESH_WASM_EXPORT_INIT, msg);
        return false;
    }
    *init = (uint32_t)f;
    f = wasm_instance_func(inst, MESH_WASM_EXPORT_TASK, "ii>i", msg, sizeof msg);
    if (f < 0) {
        if (err) *err = mesh_wasm_fmt("load: %s: %s: %s", p->name, MESH_WASM_EXPORT_TASK, msg);
        return false;
    }
    *task = (uint32_t)f;
    return true;
}

void mesh_wasm_program_free(mesh_wasm_program *p) {
    if (!p || atomic_fetch_sub(&p->refs, 1) > 1) return;
    wasm_compiled_free(p->module);
    free(p->name);
    free(p);
}

/* ---- the kernel of a wasm slot ---------------------------------------------------------- */

static bool k_zero(const mesh_kernel *k, uint32_t *out, char **err) {
    (void)k, (void)err;
    *out = 0;
    return true;
}
/* A lane is a thread: nothing is resident (reported as 1, never 0 = "does not fit"). */
static bool k_one(const mesh_kernel *k, uint32_t *out, char **err) {
    (void)k, (void)err;
    *out = 1;
    return true;
}
static uint64_t k_scratch(const mesh_kernel *k) {
    (void)k;
    return 0;
}
static bool k_global(const mesh_kernel *k, const char *name, uint32_t *out, char **err) {
    const mesh_wasm_program *p = k->self;
    if (strcmp(name, MESH_RT_ABI_SYMBOL) == 0) {
        *out = p->rt_abi;
        return true;
    }
    if (err) *err = mesh_wasm_fmt("global %s: a wasm image reports only %s (its mesh_rt_abi export)", name, MESH_RT_ABI_SYMBOL);
    return false;
}
static void k_drop(mesh_kernel *k) { mesh_wasm_program_free(k->self); }

const mesh_kernel_ops mesh_wasm_kernel_ops = {MESH_BACKEND_WASM, k_zero, k_zero, k_one, k_scratch, k_global, k_drop};

const mesh_wasm_program *mesh_wasm_kernel_program(const mesh_kernel *k) {
    return k && k->ops == &mesh_wasm_kernel_ops ? k->self : NULL;
}

/* ---- the compiler ----------------------------------------------------------------------- */

static bool c_needs_ptx(const mesh_compiler *c) {
    (void)c;
    return false;
}

static bool read_file(const char *path, uint8_t **out, size_t *n, char **why) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        *why = mesh_wasm_fmt("%s", strerror(errno));
        return false;
    }
    uint8_t *b = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            cap = cap ? cap * 2 : 65536;
            uint8_t *q = realloc(b, cap);
            if (!q) break;
            b = q;
        }
        size_t k = fread(b + len, 1, cap - len, f);
        len += k;
        if (k == 0) break;
    }
    bool ok = !ferror(f) && b;
    if (!ok) *why = mesh_wasm_fmt("%s", ferror(f) ? strerror(errno) : "out of memory");
    fclose(f);
    if (!ok) {
        free(b);
        return false;
    }
    *out = b;
    *n = len;
    return true;
}

static bool c_prepare(const mesh_compiler *c, mesh_payload *src, const uint64_t *expect, const char *root, mesh_prepared *out,
                      char **err) {
    mesh_wasm_rt *rt = c->self;
    mesh_manifest m;
    memset(out, 0, sizeof *out);
    if (!mesh_check_manifest(src, MESH_BACKEND_WASM, &m, err)) {
        mesh_payload_free(src);
        return false;
    }
    bool ok = false;
    char *dir = NULL, *path = NULL, *why = NULL;
    uint8_t *wasm = NULL;
    size_t wlen = 0;
    mesh_wasm_program *p = NULL;
    wasm_instance *inst = NULL;
    mesh_wasm_lane_data *data = NULL;
    uint8_t *scratch = NULL;
    if (!(dir = mesh_loader_resolve(root, src->dir, err))) goto out;
    path = mesh_wasm_fmt("%s/%s.wasm", dir, m.name);
    if (!read_file(path, &wasm, &wlen, &why)) {
        if (err) *err = mesh_wasm_fmt("manifest: %s declares backend wasm, but %s: %s", m.name, path, why);
        goto out;
    }
    const char *what = src->ptx_len == 0 ? "wasm" : "PTX";
    uint64_t hash = src->ptx_len == 0 ? mesh_image_hash(wasm, wlen, m.param) : mesh_image_hash(src->ptx, src->ptx_len, m.param);
    if (!mesh_check_hash(m.name, what, hash, expect, err)) goto out;
    wasm_float_report fr;
    char msg[1024];
    if (!wasm_float_check(wasm, wlen, m.ftz, &fr, msg, sizeof msg)) {
        if (err) *err = mesh_wasm_fmt("%s (%s)", msg, path);
        goto out;
    }
    double compile_ms = 0;
    const char *how = "compiled";
    wasm_compiled *module = mesh_wasm_rt_module(rt, m.name, wasm, wlen, &compile_ms, &how, err);
    if (!module) goto out;
    if (!(p = calloc(1, sizeof *p))) {
        wasm_compiled_free(module);
        goto out;
    }
    atomic_init(&p->refs, 1);
    p->name = strdup(m.name);
    p->module = module;
    p->rt = rt;
    p->wasm_hash = mesh_fnv1a64(wasm, wlen);
    p->mem_bytes = (uint64_t)m.mem_mb << 20;
    p->task_ms = m.task_ms;
    p->float_report = fr;
    /* MESH_RT_ABI: the image's mesh_rt_abi export, in a scratch instance (which also proves the
     * exports and the memory cap fit). */
    if (!(scratch = calloc(1, MESH_MAILBOX_BYTES)) || !(data = malloc(sizeof *data))) {
        if (err) *err = mesh_wasm_fmt("load: %s: out of memory", m.name);
        goto out;
    }
    if (!(inst = mesh_wasm_instantiate(p, mesh_mailbox_new(scratch, 0), 0, 0, 1, data, err))) goto out;
    uint32_t init, task;
    if (!mesh_wasm_entry_points(p, inst, &init, &task, err)) goto out;
    int64_t g = wasm_instance_func(inst, MESH_WASM_EXPORT_RT_ABI, ">i", msg, sizeof msg);
    if (g < 0) {
        if (err) *err = mesh_wasm_fmt("abi: %s: no %s export: %s", m.name, MESH_WASM_EXPORT_RT_ABI, msg);
        goto out;
    }
    uint64_t io[1] = {0};
    int trap = wasm_call(inst, (uint32_t)g, io);
    if (trap != WASM_NUM_OK) {
        if (err) *err = mesh_wasm_fmt("abi: %s: %s: wasm trap: %s", m.name, MESH_WASM_EXPORT_RT_ABI, wasm_trap_message(trap));
        goto out;
    }
    p->rt_abi = (uint32_t)io[0];
    mesh_info("wasm: %s %s in %.1f ms (%zu bytes of wasm, wasm hash %016llx, image hash %016llx from its %s; float %s: %u f32 "
              "ops, all in %u %s* helpers; lanes capped at %u MiB, %u ms a task)",
              m.name, how, compile_ms, wlen, (unsigned long long)p->wasm_hash, (unsigned long long)hash, what,
              m.ftz ? "ftz" : "ieee", fr.f32_ops, fr.helpers, MESH_WASM_FTZ_PREFIX, m.mem_mb, m.task_ms);
    mesh_kernel *k = malloc(sizeof *k);
    if (!k) goto out;
    k->ops = &mesh_wasm_kernel_ops;
    k->self = p;
    out->manifest = m;
    out->hash = hash;
    out->code = wasm;
    out->code_len = wlen;
    out->compile_ms = compile_ms;
    out->src = *src;
    memset(src, 0, sizeof *src);
    out->kernel = k;
    out->exec = MESH_BACKEND_WASM;
    out->reg_cap = 0;
    wasm = NULL;
    p = NULL;
    ok = true;
out:
    wasm_instance_free(inst);
    free(data);
    free(scratch);
    mesh_wasm_program_free(p);
    free(wasm);
    free(path);
    free(dir);
    free(why);
    if (!ok) {
        mesh_manifest_free(&m);
        mesh_payload_free(src);
    }
    return ok;
}

const mesh_compiler_ops mesh_wasm_compiler_ops = {c_needs_ptx, c_prepare};
