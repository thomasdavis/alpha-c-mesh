/* nvjitlink.rs: open, version check, one handle per link (destroyed on every path). */
#include "mesh/nvjitlink.h"
#include "mesh/text.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_PTX 2 /* NVJITLINK_INPUT_PTX */

static void *sym(void *lib, const char *path, const char *name, char **err) {
    void *p = dlsym(lib, name);
    if (!p) mesh_errf(err, "nvJitLink: %s has no %s", path, name);
    return p;
}

bool mesh_nvjitlink_open(const char *dir, mesh_nvjitlink *out, char **err) {
    *out = (mesh_nvjitlink){0};
    size_t dn = strlen(dir);
    while (dn > 0 && dir[dn - 1] == '/') dn--;
    char *path = mesh_fmt("%.*s/%s", (int)dn, dir, MESH_NVJITLINK_LIB);
    void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        const char *e = dlerror();
        mesh_errf(err, "nvJitLink: cannot open %s: %s (set --nvjitlink DIR; there is no fallback)", path, e ? e : "");
        free(path);
        return false;
    }
    void *ver = sym(lib, path, "nvJitLinkVersion", err);
    if (!ver) {
        free(path);
        return false;
    }
    int (*version)(uint32_t *, uint32_t *);
    memcpy(&version, &ver, sizeof ver);
    uint32_t major = 0, minor = 0;
    if (version(&major, &minor) != 0) {
        mesh_errf(err, "nvJitLink: nvJitLinkVersion failed (%s)", path);
        free(path);
        return false;
    }
    if (major != 13) {
        mesh_errf(err, "nvJitLink: %s is version %u.%u; CUDA 13 needed", path, (unsigned)major, (unsigned)minor);
        free(path);
        return false;
    }
    static const char *const names[] = {"__nvJitLinkCreate_13_0", "__nvJitLinkDestroy_13_0", "__nvJitLinkAddData_13_0",
                                         "__nvJitLinkComplete_13_0", "__nvJitLinkGetLinkedCubinSize_13_0",
                                         "__nvJitLinkGetLinkedCubin_13_0", "__nvJitLinkGetErrorLogSize_13_0",
                                         "__nvJitLinkGetErrorLog_13_0"};
    void *ptrs[8];
    for (int i = 0; i < 8; i++)
        if (!(ptrs[i] = sym(lib, path, names[i], err))) {
            free(path);
            return false;
        }
    memcpy(&out->fns.create, &ptrs[0], sizeof(void *));
    memcpy(&out->fns.destroy, &ptrs[1], sizeof(void *));
    memcpy(&out->fns.add_data, &ptrs[2], sizeof(void *));
    memcpy(&out->fns.complete, &ptrs[3], sizeof(void *));
    memcpy(&out->fns.cubin_size, &ptrs[4], sizeof(void *));
    memcpy(&out->fns.cubin, &ptrs[5], sizeof(void *));
    memcpy(&out->fns.err_size, &ptrs[6], sizeof(void *));
    memcpy(&out->fns.err_log, &ptrs[7], sizeof(void *));
    out->path = path;
    out->major = major;
    out->minor = minor;
    return true;
}

void mesh_nvjitlink_from(const mesh_nvjitlink_fns *fns, mesh_nvjitlink *out) {
    *out = (mesh_nvjitlink){mesh_strdup("(test)"), 13, 0, *fns};
}

void mesh_nvjitlink_free(mesh_nvjitlink *j) {
    free(j->path);
    *j = (mesh_nvjitlink){0};
}

/* The error log, trimmed of NULs and whitespace ("" when there is none). */
static char *error_log(const mesh_nvjitlink *j, void *h) {
    size_t n = 0;
    if (j->fns.err_size(h, &n) != 0 || n == 0) return mesh_strdup("");
    char *buf = mesh_alloc(n + 1);
    if (j->fns.err_log(h, buf) != 0) {
        free(buf);
        return mesh_strdup("");
    }
    size_t len = n;
    while (len > 0 && buf[len - 1] == 0) len--;
    const char *s = buf;
    mesh_trim(&s, &len);
    char *r = mesh_strndup(s, len);
    free(buf);
    return r;
}

bool mesh_nvjitlink_ptx_to_cubin(const mesh_nvjitlink *j, const uint8_t *ptx, size_t n, const char *arch, uint32_t reg_cap,
                                 uint8_t **cubin, size_t *cubin_len, char **err) {
    char *opt_arch = mesh_fmt("-arch=%s", arch);
    char *opt_reg = reg_cap ? mesh_fmt("-maxrregcount=%u", (unsigned)reg_cap) : NULL;
    const char *opts[2] = {opt_arch, opt_reg};
    void *h = NULL;
    int r = j->fns.create(&h, opt_reg ? 2 : 1, opts);
    free(opt_arch);
    free(opt_reg);
    if (r != 0) return mesh_errf(err, "nvJitLinkCreate(%s) failed: code %d", arch, r);
    bool ok = false;
    /* PTX is text: hand it NUL-terminated. */
    bool nul = n > 0 && ptx[n - 1] == 0;
    uint8_t *src = mesh_alloc(n + 1);
    memcpy(src, ptx, n);
    size_t sn = nul ? n : n + 1;
    r = j->fns.add_data(h, INPUT_PTX, src, sn, "program.ptx");
    if (r != 0) {
        char *log = error_log(j, h);
        mesh_errf(err, "nvJitLinkAddData failed: code %d: %s", r, log);
        free(log);
        goto done;
    }
    r = j->fns.complete(h);
    if (r != 0) {
        char *log = error_log(j, h);
        mesh_errf(err, "nvJitLinkComplete failed: code %d: %s", r, log);
        free(log);
        goto done;
    }
    size_t len = 0;
    r = j->fns.cubin_size(h, &len);
    if (r != 0 || len == 0) {
        mesh_errf(err, "nvJitLinkGetLinkedCubinSize failed: code %d", r);
        goto done;
    }
    uint8_t *out = mesh_alloc(len);
    r = j->fns.cubin(h, out);
    if (r != 0) {
        free(out);
        mesh_errf(err, "nvJitLinkGetLinkedCubin failed: code %d", r);
        goto done;
    }
    *cubin = out;
    *cubin_len = len;
    ok = true;
done:
    free(src);
    j->fns.destroy(&h);
    return ok;
}
