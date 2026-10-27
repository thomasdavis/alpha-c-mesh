/* args.rs, flag by flag. */
#include "mesh/args.h"
#include "mesh/abi.h"
#include "mesh/buf.h"
#include "mesh/rsfmt.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const CUDA_FLAGS[6] = {"--sms", "--nvjitlink", "--cuda-lib", "--isolation", "--mps-dir", "--mps-pool"};

static char *q(const char *s) {
    mesh_buf b = MESH_BUF_INIT;
    mesh_rs_debug_cstr(&b, s);
    return mesh_buf_take(&b);
}

int mesh_args_backend_of(int argc, char **argv, uint32_t *kind, char **err) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--backend") != 0) continue;
        if (i + 1 >= argc) { *err = mesh_xstrdup("--backend needs a value"); return -1; }
        const char *v = argv[i + 1];
        if (!strcmp(v, "cuda")) *kind = MESH_BACKEND_CUDA;
        else if (!strcmp(v, "metal")) *kind = MESH_BACKEND_METAL;
        else if (!strcmp(v, "wasm")) *kind = MESH_BACKEND_WASM;
        else {
            char *qv = q(v);
            *err = mesh_buf_fmt("--backend %s: cuda, metal or wasm", qv);
            free(qv);
            return -1;
        }
        return 0;
    }
    *kind = MESH_DEFAULT_BACKEND;
    return 0;
}

static void set(char **dst, const char *v) {
    free(*dst);
    *dst = v ? mesh_xstrdup(v) : NULL;
}

static char *slurp(const char *path, char **err) {
    FILE *f = fopen(path, "r");
    if (!f) { *err = mesh_buf_fmt("%s (os error %d)", strerror(errno), errno); return NULL; }
    mesh_buf b = MESH_BUF_INIT;
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) mesh_buf_put(&b, chunk, n);
    fclose(f);
    mesh_buf_reserve(&b, 0);
    return mesh_buf_take(&b);
}

int mesh_args_parse(int argc, char **argv, mesh_args *a, char **err) {
    memset(a, 0, sizeof *a);
    if (mesh_args_backend_of(argc, argv, &a->backend, err)) return -1;
    a->control = mesh_buf_fmt("127.0.0.1:%u", (unsigned)MESH_CONTROL_PORT);
    a->http = mesh_buf_fmt("0.0.0.0:%u", (unsigned)MESH_HTTP_PORT);
    a->nvjitlink = mesh_xstrdup(MESH_NVJITLINK_DIR);
    a->programs = mesh_xstrdup("out/programs");
    a->isolation = MESH_ISOLATION_MODE_BLINK;
    a->logs = mesh_xstrdup("logs");
    a->blobs_cap_mb = 4096;
    /* (flag, value): --peer, --peer-listen or --cluster, resolved once --system is known. */
    const char *lflag = NULL, *lval = NULL;
    for (int i = 0; i < argc; i++) {
        const char *flag = argv[i];
        if (a->backend != MESH_BACKEND_CUDA)
            for (int k = 0; k < 6; k++)
                if (!strcmp(flag, CUDA_FLAGS[k])) {
                    *err = mesh_buf_fmt("%s: a CUDA flag; --backend %s has no such setting (no silent ignoring)", flag, MESH_BACKENDS[a->backend]);
                    return -1;
                }
#define VAL(v)                                                                  \
    const char *v = i + 1 < argc ? argv[++i] : NULL;                           \
    if (!v) { *err = mesh_buf_fmt("%s needs a value", flag); return -1; }
#define NUM(v, out)                                                             \
    do {                                                                        \
        if (mesh_rs_parse_u32((v), strlen(v), (out))) {                         \
            char *qv = q(v);                                                    \
            *err = mesh_buf_fmt("%s: bad number %s", flag, qv);                 \
            free(qv);                                                           \
            return -1;                                                          \
        }                                                                       \
    } while (0)
        if (!strcmp(flag, "--backend")) { VAL(v); (void)v; }
        else if (!strcmp(flag, "--sms")) { VAL(v); NUM(v, &a->sms); a->has_sms = true; }
        else if (!strcmp(flag, "--control")) { VAL(v); set(&a->control, v); }
        else if (!strcmp(flag, "--http")) { VAL(v); set(&a->http, v); }
        else if (!strcmp(flag, "--system")) { VAL(v); NUM(v, &a->system); a->has_system = true; }
        else if (!strcmp(flag, "--peer") || !strcmp(flag, "--peer-listen") || !strcmp(flag, "--cluster")) {
            if (lflag) { *err = mesh_buf_fmt("%s: %s given already (one of --cluster, --peer, --peer-listen)", flag, lflag); return -1; }
            VAL(v);
            lflag = flag;
            lval = v;
        } else if (!strcmp(flag, "--nvjitlink")) { VAL(v); set(&a->nvjitlink, v); }
        else if (!strcmp(flag, "--cuda-lib")) { VAL(v); (void)v; } /* handled before parsing (mesh_cuda_use_lib re-executes meshd with it) */
        else if (!strcmp(flag, "--programs")) { VAL(v); set(&a->programs, v); }
        else if (!strcmp(flag, "--state")) { VAL(v); set(&a->state, v); }
        else if (!strcmp(flag, "--autoload")) a->autoload = true;
        else if (!strcmp(flag, "--wasm-cache")) { VAL(v); set(&a->wasm_cache, v); }
        else if (!strcmp(flag, "--isolation")) {
            VAL(v);
            if (!strcmp(v, "blink")) a->isolation = MESH_ISOLATION_MODE_BLINK;
            else if (!strcmp(v, "mps")) a->isolation = MESH_ISOLATION_MODE_MPS;
            else { char *qv = q(v); *err = mesh_buf_fmt("--isolation %s: blink or mps", qv); free(qv); return -1; }
        } else if (!strcmp(flag, "--mps-dir")) { VAL(v); set(&a->mps_dir, v); }
        else if (!strcmp(flag, "--mps-pool")) { VAL(v); NUM(v, &a->mps_pool); a->has_mps_pool = true; }
        else if (!strcmp(flag, "--logs")) { VAL(v); set(&a->logs, v); }
        else if (!strcmp(flag, "--fleet-state")) { VAL(v); set(&a->fleet_state, v); }
        else if (!strcmp(flag, "--jobs-state")) { VAL(v); set(&a->jobs_state, v); }
        else if (!strcmp(flag, "--blobs")) { VAL(v); set(&a->blobs, v); }
        else if (!strcmp(flag, "--blobs-cap")) { VAL(v); uint32_t mb; NUM(v, &mb); a->blobs_cap_mb = mb; }
        else if (!strcmp(flag, "--models")) { VAL(v); set(&a->models, v); }
        else if (!strcmp(flag, "-h") || !strcmp(flag, "--help")) { *err = mesh_xstrdup(MESH_ARGS_USAGE); return -1; }
        else {
            char *qf = q(flag);
            *err = mesh_buf_fmt("unknown flag %s\n%s", qf, MESH_ARGS_USAGE);
            free(qf);
            return -1;
        }
#undef VAL
#undef NUM
    }
    if (!lflag && a->has_system) { *err = mesh_xstrdup("--system needs --cluster FILE (or --peer for system 0, --peer-listen for system 1)"); return -1; }
    if (lflag && !a->has_system) { *err = mesh_buf_fmt("%s needs --system K", lflag); return -1; }
    if (lflag) {
        uint32_t k = a->system;
        if (!strcmp(lflag, "--peer") && k == 0) a->topo = mcc_pair_dialer(lval);
        else if (!strcmp(lflag, "--peer-listen") && k == 1) a->topo = mcc_pair_listener(lval);
        else if (!strcmp(lflag, "--peer")) { *err = mesh_xstrdup("--peer is for --system 0"); return -1; }
        else if (!strcmp(lflag, "--peer-listen")) { *err = mesh_xstrdup("--peer-listen is for --system 1"); return -1; }
        else {
            char *e = NULL;
            char *text = slurp(lval, &e);
            if (!text) { *err = mesh_buf_fmt("--cluster %s: %s", lval, e); free(e); return -1; }
            if (!mcc_keys(text, &a->keys, &e)) { *err = mesh_buf_fmt("--cluster %s: %s", lval, e); free(e); free(text); return -1; }
            a->has_keys = true;
            if (!mcc_topology_parse(text, k, &a->topo, &e)) { *err = mesh_buf_fmt("--cluster %s: %s", lval, e); free(e); free(text); return -1; }
            free(text);
        }
        a->cluster = true;
    }
    if (a->has_sms && a->sms == 0) { *err = mesh_xstrdup("--sms must be > 0"); return -1; }
    if (a->cluster && !a->fleet_state) {
        *err = mesh_xstrdup("a fleet member needs --fleet-state FILE (its term, vote and desired state survive restarts there)");
        return -1;
    }
    if (!a->cluster && a->fleet_state) { *err = mesh_xstrdup("--fleet-state is for a fleet member (--cluster / --peer)"); return -1; }
    /* In a fleet the job table's log and the blobs sit beside the fleet state (they must survive restarts as that does). */
    if (!a->jobs_state && a->fleet_state) a->jobs_state = mesh_buf_fmt("%s.jobs", a->fleet_state);
    if (!a->blobs && a->fleet_state) a->blobs = mesh_buf_fmt("%s.blobs", a->fleet_state);
    if ((a->mps_dir || a->has_mps_pool) && a->isolation != MESH_ISOLATION_MODE_MPS) {
        *err = mesh_xstrdup("--mps-dir and --mps-pool are for --isolation mps");
        return -1;
    }
    return 0;
}

void mesh_args_free(mesh_args *a) {
    free(a->wasm_cache);
    free(a->control);
    free(a->http);
    if (a->cluster) mcc_topology_free(&a->topo);
    free(a->nvjitlink);
    free(a->programs);
    free(a->state);
    free(a->mps_dir);
    free(a->logs);
    free(a->fleet_state);
    free(a->jobs_state);
    free(a->blobs);
    free(a->models);
    if (a->has_keys) ms_free(&a->keys);
    memset(a, 0, sizeof *a);
}
