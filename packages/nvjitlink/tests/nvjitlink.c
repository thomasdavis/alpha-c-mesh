/* nvjitlink.rs's link_and_refuse, with a fake nvJitLink (the real one needs the
 * CUDA 13 toolkit, which this machine lacks; on a DGX the open succeeds). */
#include "mesh/check.h"
#include "mesh/nvjitlink.h"

#include <stdlib.h>
#include <string.h>

static int created, destroyed;
static char opts_seen[128];
static size_t added;
static int fail_complete;

static int f_create(void **h, uint32_t n, const char *const *opts) {
    created++;
    opts_seen[0] = 0;
    for (uint32_t i = 0; i < n; i++) {
        strcat(opts_seen, opts[i]);
        strcat(opts_seen, " ");
    }
    *h = &created;
    return 0;
}
static int f_destroy(void **h) {
    destroyed++;
    *h = NULL;
    return 0;
}
static int f_add(void *h, int kind, const void *data, size_t n, const char *name) {
    (void)h;
    CHECK_I(kind, 2);
    CHECK_S(name, "program.ptx");
    CHECK_U(((const char *)data)[n - 1], 0);
    added = n;
    return 0;
}
static int f_complete(void *h) {
    (void)h;
    return fail_complete;
}
static int f_size(void *h, size_t *n) {
    (void)h;
    *n = 8;
    return 0;
}
static int f_cubin(void *h, void *out) {
    (void)h;
    memcpy(out, "\x7f" "ELFcubn", 8);
    return 0;
}
static int f_err_size(void *h, size_t *n) {
    (void)h;
    *n = 24;
    return 0;
}
static int f_err_log(void *h, void *out) {
    (void)h;
    memcpy(out, "ptxas: bogus opcode \n\0\0\0", 24);
    return 0;
}

int main(void) {
    mesh_nvjitlink j;
    char *err = NULL;
    CHECK(!mesh_nvjitlink_open("/nonexistent", &j, &err));
    CHECK(strstr(err, "cannot open /nonexistent/libnvJitLink.so.13") != NULL);
    CHECK(strstr(err, "(set --nvjitlink DIR; there is no fallback)") != NULL);
    free(err);
    err = NULL;
    mesh_nvjitlink_fns f = {f_create, f_destroy, f_add, f_complete, f_size, f_cubin, f_err_size, f_err_log};
    mesh_nvjitlink_from(&f, &j);
    const char *ptx = ".version 8.8\n.target sm_121\n.address_size 64\n.visible .entry k() { ret; }\n";
    uint8_t *cubin;
    size_t n;
    CHECK(mesh_nvjitlink_ptx_to_cubin(&j, (const uint8_t *)ptx, strlen(ptx), "sm_121", 0, &cubin, &n, &err));
    CHECK(n == 8 && memcmp(cubin, "\x7f" "ELF", 4) == 0);
    CHECK_S(opts_seen, "-arch=sm_121 ");
    CHECK_U(added, strlen(ptx) + 1);
    free(cubin);
    CHECK(mesh_nvjitlink_ptx_to_cubin(&j, (const uint8_t *)ptx, strlen(ptx), "sm_86", 64, &cubin, &n, &err));
    CHECK_S(opts_seen, "-arch=sm_86 -maxrregcount=64 ");
    free(cubin);
    fail_complete = 7;
    CHECK(!mesh_nvjitlink_ptx_to_cubin(&j, (const uint8_t *)ptx, strlen(ptx), "sm_121", 0, &cubin, &n, &err));
    CHECK_S(err, "nvJitLinkComplete failed: code 7: ptxas: bogus opcode");
    free(err);
    CHECK_I(created, destroyed);
    mesh_nvjitlink_free(&j);
    return mesh_check_done();
}
