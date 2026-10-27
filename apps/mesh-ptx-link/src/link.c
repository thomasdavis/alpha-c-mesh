/* The cluster float mode is one flag, --denormal-fp-math-f32=preserve-sign:
 * opt stamps it on every function ("denormal-fp-math-f32"="preserve-sign,
 * preserve-sign"), so the optimiser folds f32 constants with flushing and
 * the NVPTX backend selects the .ftz form of every f32 instruction that has
 * one. f32 subnormal inputs and results become sign-preserving zero, as Apple
 * GPUs always do, so CUDA and Metal members compute the same bits. f64 is
 * untouched (PTX has no f64 ftz). llc also gets --fp-contract=off: rustc never
 * contracts, but clang's C may emit llvm.fmuladd in a library compiled without
 * -ffp-contract=off, and strict contraction lowers that to a multiply and an add. */
#include "link.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DENORM "--denormal-fp-math-f32=preserve-sign"

static char *sdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (!d) abort();
    memcpy(d, s, n);
    return d;
}
static char *fmt2(const char *a, const char *b) {
    size_t n = strlen(a) + strlen(b) + 1;
    char *d = malloc(n);
    if (!d) abort();
    snprintf(d, n, "%s%s", a, b);
    return d;
}

typedef struct {
    char **v;
    size_t n, cap;
} argv_buf;
static void push(argv_buf *a, char *s) {
    if (a->n + 2 > a->cap) {
        a->cap = a->cap ? 2 * a->cap : 16;
        a->v = realloc(a->v, a->cap * sizeof *a->v);
        if (!a->v) abort();
    }
    a->v[a->n++] = s;
    a->v[a->n] = NULL;
}

char *mesh_ptx_link_side(const mesh_ptx_link *l, const char *suffix) {
    size_t n = strlen(l->output);
    char *base = sdup(l->output);
    if (n >= 4 && strcmp(base + n - 4, ".ptx") == 0) base[n - 4] = 0;
    char *r = fmt2(base, suffix);
    free(base);
    return r;
}

char **mesh_ptx_link_argv_link(const mesh_ptx_link *l) {
    argv_buf a = {0};
    push(&a, sdup(l->llvm_link));
    push(&a, sdup("--ignore-non-bitcode"));
    for (size_t i = 0; i < l->ninputs; i++) push(&a, sdup(l->inputs[i]));
    push(&a, sdup("-o"));
    push(&a, mesh_ptx_link_side(l, ".o"));
    return a.v;
}

char **mesh_ptx_link_argv_opt(const mesh_ptx_link *l) {
    argv_buf a = {0};
    char *syms = mesh_ptx_link_side(l, ".symbols.txt");
    char passes[64];
    snprintf(passes, sizeof passes, "--passes=default<O%d>,internalize,globaldce", l->opt);
    push(&a, sdup(l->opt_tool));
    push(&a, mesh_ptx_link_side(l, ".o"));
    push(&a, sdup("-o"));
    push(&a, mesh_ptx_link_side(l, ".optimized.o"));
    push(&a, sdup(DENORM));
    push(&a, fmt2("--internalize-public-api-file=", syms));
    push(&a, sdup(passes));
    if (!l->debug) push(&a, sdup("--strip-debug"));
    free(syms);
    return a.v;
}

char **mesh_ptx_link_argv_llc(const mesh_ptx_link *l) {
    argv_buf a = {0};
    push(&a, sdup(l->llc));
    push(&a, sdup(DENORM));
    push(&a, sdup("--fp-contract=off"));
    push(&a, sdup("--mcpu"));
    push(&a, sdup(l->cpu));
    push(&a, mesh_ptx_link_side(l, ".optimized.o"));
    push(&a, sdup("-o"));
    push(&a, sdup(l->output));
    return a.v;
}

void mesh_ptx_link_free_argv(char **argv) {
    if (!argv) return;
    for (char **p = argv; *p; p++) free(*p);
    free(argv);
}

char *mesh_ptx_link_symbols(const mesh_ptx_link *l) {
    static const char *const def[] = {"mesh_main", "MESH_RT_ABI"};
    const char *const *e = l->exports ? l->exports : def;
    size_t n = l->exports ? l->nexports : 2, len = 1;
    for (size_t i = 0; i < n; i++) len += strlen(e[i]) + 1;
    char *s = malloc(len), *p = s;
    if (!s) abort();
    for (size_t i = 0; i < n; i++) p += sprintf(p, "%s\n", e[i]);
    *p = 0;
    return s;
}

static bool executable(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, X_OK) == 0;
}

static char *on_path(const char *name) {
    const char *path = getenv("PATH");
    if (!path) path = "/usr/bin:/bin";
    while (*path) {
        const char *c = strchr(path, ':');
        size_t n = c ? (size_t)(c - path) : strlen(path);
        char buf[4096];
        if (n > 0 && n + strlen(name) + 2 < sizeof buf) {
            snprintf(buf, sizeof buf, "%.*s/%s", (int)n, path, name);
            if (executable(buf)) return sdup(buf);
        }
        path += n + (c ? 1 : 0);
        if (!c) break;
    }
    return NULL;
}

char *mesh_ptx_link_find_tool(const char *name, const char *env) {
    const char *e = env ? getenv(env) : NULL;
    if (e && *e) return sdup(e);
    char *p = on_path(name);
    if (p) return p;
    for (int v = 21; v >= 15; v--) {
        char n[128];
        snprintf(n, sizeof n, "%s-%d", name, v);
        if ((p = on_path(n))) return p;
    }
    /* The newest /usr/lib/llvm-N/bin. */
    int best = -1;
    DIR *d = opendir("/usr/lib");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            int v;
            char tail;
            if (sscanf(de->d_name, "llvm-%d%c", &v, &tail) == 1 && v > best) {
                char buf[512];
                snprintf(buf, sizeof buf, "/usr/lib/llvm-%d/bin/%s", v, name);
                if (executable(buf)) best = v;
            }
        }
        closedir(d);
    }
    if (best >= 0) {
        char buf[512];
        snprintf(buf, sizeof buf, "/usr/lib/llvm-%d/bin/%s", best, name);
        return sdup(buf);
    }
    return NULL;
}

bool mesh_ptx_link_valid_arch(const char *a) {
    if (strncmp(a, "sm_", 3) != 0) return false;
    size_t i = 3, d = 0;
    while (a[i] >= '0' && a[i] <= '9') i++, d++;
    if (d < 2 || d > 3) return false;
    if (a[i] == 'a' || a[i] == 'f') i++;
    return a[i] == 0;
}
