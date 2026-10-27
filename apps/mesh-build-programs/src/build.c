/* The program catalog: for each program, its images under <out>/<name>/ beside
 * its program.toml -- the GPU image <name>.ptx (the sexc ptx package's bitcode
 * linked by mesh-ptx-link in the cluster float mode, then checked: `.target
 * <arch>` exactly, the float rules, mesh_main, MESH_RT_ABI), the wasm image
 * <name>.wasm (checked for its mesh_init, mesh_task, mesh_rt_abi exports), a
 * native program's binary <triple>/<name> and its source.hash -- then its
 * variants (programs/<name>/variants/<v>.toml: other names for the same images).
 * Everything lands by rename (tmp then mv), so a running meshd never reads half
 * a file. Ported from alpha-rust-mesh scripts/build-programs.sh. */
#include "build.h"

#include "mesh/catalog_toml.h"
#include "mesh/ptx_float_rules.h"
#include "mesh/source_hash.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

#define MBP_MAX 256

static char *fmt(const char *f, ...) {
    va_list a, b;
    va_start(a, f);
    va_copy(b, a);
    int n = vsnprintf(NULL, 0, f, a);
    va_end(a);
    char *s = malloc((size_t)n + 1);
    if (!s) abort();
    vsnprintf(s, (size_t)n + 1, f, b);
    va_end(b);
    return s;
}

static int fail(const char *f, ...) {
    va_list a;
    va_start(a, f);
    fputs("error: ", stderr);
    vfprintf(stderr, f, a);
    fputc('\n', stderr);
    va_end(a);
    return 1;
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    char *b = malloc(cap + 1);
    for (;;) {
        if (!b) break;
        n += fread(b + n, 1, cap - n, f);
        if (n < cap) break;
        cap *= 2;
        char *nb = realloc(b, cap + 1);
        if (!nb) { free(b); b = NULL; break; }
        b = nb;
    }
    bool bad = ferror(f);
    fclose(f);
    if (!b || bad) { free(b); return NULL; }
    b[n] = 0;
    if (len) *len = n;
    return b;
}

static bool is_file(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}
static bool mkdirs(const char *p) {
    char *c = fmt("%s", p);
    for (char *s = c + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            mkdir(c, 0755);
            *s = '/';
        }
    bool ok = mkdir(c, 0755) == 0 || errno == EEXIST;
    free(c);
    return ok;
}
/* cp src dst.tmp && mv dst.tmp dst, keeping the executable bit. */
static bool install(const char *src, const char *dst) {
    size_t n;
    char *b = slurp(src, &n);
    if (!b) return false;
    struct stat st;
    mode_t mode = stat(src, &st) == 0 ? (st.st_mode & 0777) : 0644;
    char *tmp = fmt("%s.tmp", dst);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(b, 1, n, f) == n;
    if (f) ok = (fclose(f) == 0) && ok;
    ok = ok && chmod(tmp, mode) == 0 && rename(tmp, dst) == 0;
    free(tmp);
    free(b);
    return ok;
}

char *mbp_pkg_id(const char *name) {
    char *s = fmt("%s", name[0] == '@' ? name + 1 : name);
    for (char *p = s; *p; p++)
        if (*p == '/') *p = '-';
    return s;
}

bool mbp_valid_arch(const char *a) {
    if (strncmp(a, "sm_", 3) != 0) return false;
    size_t i = 3, d = 0;
    while (a[i] >= '0' && a[i] <= '9') i++, d++;
    if (d < 2 || d > 3) return false;
    if (a[i] == 'a' || a[i] == 'f') i++;
    return a[i] == 0;
}

const char *mbp_triple(void) {
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

char *mbp_json_field(const char *json, const char *key) {
    char *pat = fmt("\"%s\"", key);
    const char *p = strstr(json, pat);
    free(pat);
    if (!p) return NULL;
    p += strlen(key) + 2;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return NULL;
    const char *e = strchr(p + 1, '"');
    if (!e) return NULL;
    return fmt("%.*s", (int)(e - p - 1), p + 1);
}

/* Runs argv (inheriting stdout and stderr) with one extra environment entry; its exit status. */
static int run(char *const *argv, const char *env) {
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) return fail("fork: %s", strerror(errno));
    if (pid == 0) {
        if (env) putenv((char *)env);
        execvp(argv[0], argv);
        fprintf(stderr, "error: %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    int st;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR) return 1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

/* ---- the programs under <root>/programs */
typedef struct {
    char *name;          /* program.toml's name */
    char *toml_dir;      /* programs/<dir> holding the manifest (a -wasm dir only for a wasm-only program) */
    char *toml;          /* its text */
    char *gpu, *native, *wasm; /* the sexc package of each image */
    char *native_bin;
} program;

typedef struct {
    program v[MBP_MAX];
    size_t n;
} programs;

static program *find_prog(programs *ps, const char *name) {
    for (size_t i = 0; i < ps->n; i++)
        if (strcmp(ps->v[i].name, name) == 0) return &ps->v[i];
    if (ps->n == MBP_MAX) return NULL;
    program *p = &ps->v[ps->n++];
    memset(p, 0, sizeof *p);
    p->name = fmt("%s", name);
    return p;
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static int scan(const mbp_opts *o, programs *ps) {
    char *dir = fmt("%s/programs", o->root);
    DIR *d = opendir(dir);
    if (!d) { int r = fail("no programs under %s", dir); free(dir); return r; }
    char *names[MBP_MAX];
    size_t nn = 0;
    struct dirent *e;
    while ((e = readdir(d)) && nn < MBP_MAX)
        if (e->d_name[0] != '.') names[nn++] = fmt("%s", e->d_name);
    closedir(d);
    qsort(names, nn, sizeof *names, by_name);
    int rc = 0;
    for (size_t i = 0; i < nn && rc == 0; i++) {
        char *toml_path = fmt("%s/%s/program.toml", dir, names[i]), *json_path = fmt("%s/%s/sexc.json", dir, names[i]);
        if (is_file(toml_path) && is_file(json_path)) {
            char *toml = slurp(toml_path, NULL), *json = slurp(json_path, NULL), nm[64];
            if (!toml || !json || !mesh_catalog_toml_name(toml, nm, sizeof nm)) {
                rc = fail("programs/%s/program.toml: name must be \"%s\"", names[i], names[i]);
            } else {
                size_t dl = strlen(names[i]), nl = strlen(nm);
                bool own = strcmp(nm, names[i]) == 0, wasm_dir = dl == nl + 5 && strncmp(names[i], nm, nl) == 0 && strcmp(names[i] + nl, "-wasm") == 0;
                if (!own && !wasm_dir) rc = fail("programs/%s/program.toml: name must be \"%s\"", names[i], names[i]);
                program *p = rc ? NULL : find_prog(ps, nm);
                char *pkg = mbp_json_field(json, "name"), *kind = mbp_json_field(json, "kind");
                if (p && pkg) {
                    if (own || !p->toml_dir) {
                        free(p->toml_dir), free(p->toml);
                        p->toml_dir = fmt("programs/%s", names[i]);
                        p->toml = toml;
                        toml = NULL;
                    }
                    if (kind && strcmp(kind, "ptx") == 0) p->gpu = fmt("%s", pkg);
                    else if (kind && strcmp(kind, "wasm") == 0) p->wasm = fmt("%s", pkg);
                    else if (kind && strcmp(kind, "executable") == 0) {
                        p->native = fmt("%s", pkg);
                        char *bin = mbp_json_field(json, "bin");
                        p->native_bin = bin ? bin : fmt("%s", strchr(pkg, '/') ? strchr(pkg, '/') + 1 : pkg);
                    }
                }
                free(pkg), free(kind);
            }
            free(toml), free(json);
        }
        free(toml_path), free(json_path);
    }
    for (size_t i = 0; i < nn; i++) free(names[i]);
    free(dir);
    if (rc == 0 && ps->n == 0) rc = fail("no programs under programs/");
    return rc;
}

static int sexc_build(const mbp_opts *o, const char *pkg) {
    char *env = fmt("SEXC_PTX_ARCH=%s", o->arch);
    char *argv[] = {(char *)o->sexc, (char *)"build", (char *)"-C", (char *)o->root, (char *)"--release", (char *)pkg, NULL};
    int r = run(argv, env);
    /* putenv keeps the string in the child only; ours is ours to free. */
    free(env);
    return r == 0 ? 0 : fail("sexc build %s failed", pkg);
}

/* variants NAME: programs/<dir>/variants/<v>.toml run NAME's images under the name v. */
static int variants(const mbp_opts *o, const program *p) {
    char *vdir = fmt("%s/%s/variants", o->root, p->toml_dir);
    DIR *d = opendir(vdir);
    if (!d) { free(vdir); return 0; }
    char *names[MBP_MAX];
    size_t nn = 0;
    struct dirent *e;
    while ((e = readdir(d)) && nn < MBP_MAX) {
        size_t k = strlen(e->d_name);
        if (k > 5 && strcmp(e->d_name + k - 5, ".toml") == 0) names[nn++] = fmt("%.*s", (int)(k - 5), e->d_name);
    }
    closedir(d);
    qsort(names, nn, sizeof *names, by_name);
    int rc = 0;
    for (size_t i = 0; i < nn && rc == 0; i++) {
        const char *vn = names[i];
        char *vt = fmt("%s/%s.toml", vdir, vn), *text = slurp(vt, NULL), nm[64];
        if (!text || !mesh_catalog_toml_name(text, nm, sizeof nm) || strcmp(nm, vn) != 0) rc = fail("%s: name must be \"%s\"", vt, vn);
        char *dst = fmt("%s/%s", o->out, vn);
        if (!rc && !mkdirs(dst)) rc = fail("%s: %s", dst, strerror(errno));
        const char *exts[2] = {"ptx", "wasm"};
        for (int k = 0; k < 2 && !rc; k++) {
            char *s = fmt("%s/%s/%s.%s", o->out, p->name, p->name, exts[k]), *t = fmt("%s/%s.%s", dst, vn, exts[k]);
            if (is_file(s) && !install(s, t)) rc = fail("%s: %s", t, strerror(errno));
            free(s), free(t);
        }
        /* A native program's binary (one per target triple) under the variant's name, and its source identity. */
        char *src_dir = fmt("%s/%s", o->out, p->name);
        DIR *sd = rc ? NULL : opendir(src_dir);
        struct dirent *te;
        while (sd && (te = readdir(sd))) {
            if (te->d_name[0] == '.') continue;
            char *b = fmt("%s/%s/%s", src_dir, te->d_name, p->name);
            if (is_file(b)) {
                char *td = fmt("%s/%s", dst, te->d_name), *tb = fmt("%s/%s/%s", dst, te->d_name, vn);
                if (!mkdirs(td) || !install(b, tb)) rc = fail("%s: %s", tb, strerror(errno));
                free(td), free(tb);
            }
            free(b);
        }
        if (sd) closedir(sd);
        char *sh = fmt("%s/source.hash", src_dir), *th = fmt("%s/source.hash", dst), *tt = fmt("%s/program.toml", dst);
        if (!rc && is_file(sh) && !install(sh, th)) rc = fail("%s: %s", th, strerror(errno));
        if (!rc && !install(vt, tt)) rc = fail("%s: %s", tt, strerror(errno));
        if (!rc) printf("%s: a variant of %s\n", dst, p->name);
        free(sh), free(th), free(tt), free(src_dir), free(dst), free(vt), free(text);
    }
    for (size_t i = 0; i < nn; i++) free(names[i]);
    free(vdir);
    return rc;
}

static int write_toml(const mbp_opts *o, const program *p) {
    char *src = fmt("%s/%s/program.toml", o->root, p->toml_dir), *dir = fmt("%s/%s", o->out, p->name), *dst = fmt("%s/program.toml", dir);
    int rc = !mkdirs(dir) || !install(src, dst) ? fail("%s: %s", dst, strerror(errno)) : 0;
    free(src), free(dir), free(dst);
    return rc;
}

/* The wasm image: the sexc wasm package's module, checked for the runtime's exports. */
static int build_wasm(const mbp_opts *o, const program *p) {
    if (!p->wasm) return fail("%s/program.toml lists backend wasm, but programs/ has no wasm package for %s", p->toml_dir, p->name);
    if (sexc_build(o, p->wasm)) return 1;
    char *id = mbp_pkg_id(p->wasm), *wasm = fmt("%s/.sexc/out/release/wasm/%s.wasm", o->root, id);
    size_t n;
    char *b = slurp(wasm, &n);
    int rc = b ? 0 : fail("%s: %s", wasm, strerror(errno));
    const char *syms[3] = {"mesh_init", "mesh_task", "mesh_rt_abi"};
    for (int i = 0; i < 3 && !rc; i++) {
        size_t k = strlen(syms[i]);
        bool found = false;
        for (size_t j = 0; j + k <= n && !found; j++) found = memcmp(b + j, syms[i], k) == 0;
        if (!found) rc = fail("%s exports no %s (MESH_PROGRAM missing?)", wasm, syms[i]);
    }
    char *dir = fmt("%s/%s", o->out, p->name), *dst = fmt("%s/%s.wasm", dir, p->name);
    if (!rc && (!mkdirs(dir) || !install(wasm, dst))) rc = fail("%s: %s", dst, strerror(errno));
    if (!rc) printf("%s: %zu bytes\n", dst, n);
    free(b), free(id), free(wasm), free(dir), free(dst);
    return rc;
}

/* The GPU image: link, check, install. */
static int build_ptx(const mbp_opts *o, const program *p) {
    if (!p->gpu) return fail("programs/%s has no ptx package (sexc.json \"kind\": \"ptx\")", p->name);
    if (sexc_build(o, p->gpu)) return 1;
    char *id = mbp_pkg_id(p->gpu), *bc = fmt("%s/.sexc/out/release/ptx/%s.bc", o->root, id);
    char *dir = fmt("%s/%s", o->out, p->name), *tmp = fmt("%s/%s.link.ptx", dir, p->name), *dst = fmt("%s/%s.ptx", dir, p->name);
    int rc = mkdirs(dir) ? 0 : fail("%s: %s", dir, strerror(errno));
    if (!rc) {
        char *argv[] = {(char *)o->ptx_link, (char *)"--mcpu", (char *)o->arch, (char *)"-O3", (char *)"-o", tmp, bc, NULL};
        if (run(argv, NULL) != 0) rc = fail("mesh-ptx-link %s failed", bc);
    }
    size_t n = 0;
    char *text = rc ? NULL : slurp(tmp, &n);
    if (!rc && !text) rc = fail("%s: %s", tmp, strerror(errno));
    if (!rc) {
        /* Exactly `.target <arch>`: any `, debug` (unoptimised JIT) or another arch is refused. */
        const char *t = strstr(text, "\n.target");
        t = t ? t + 1 : (strncmp(text, ".target", 7) == 0 ? text : NULL);
        size_t tl = t ? strcspn(t, "\n") : 0;
        char *tline = fmt("%.*s", (int)tl, t ? t : "");
        char *want = fmt(".target %s", o->arch);
        if (strstr(tline, "debug")) rc = fail("%s: '%s' targets debug (unoptimised JIT)", tmp, tline);
        else if (strcmp(tline, want) != 0) rc = fail("%s: '%s', want '%s'", tmp, tline, want);
        free(tline), free(want);
    }
    /* The cluster float mode: refused, not rewritten. */
    if (!rc && mesh_ptx_float_check(tmp, text, n, NULL) != 0) {
        fprintf(stderr, "error: %s: float op outside the cluster float mode (FTZ, correctly rounded, unfused):\n", tmp);
        mesh_ptx_float_check(tmp, text, n, stderr);
        rc = 1;
    }
    if (!rc && !strstr(text, "\n.visible .entry mesh_main")) rc = fail("%s has no mesh_main entry (MESH_PROGRAM missing?)", tmp);
    if (!rc && !strstr(text, "MESH_RT_ABI")) rc = fail("%s has no MESH_RT_ABI global", tmp);
    if (!rc && rename(tmp, dst) != 0) rc = fail("%s: %s", dst, strerror(errno));
    if (!rc) {
        size_t lines = 0;
        for (size_t i = 0; i < n; i++) lines += text[i] == '\n';
        rc = write_toml(o, p);
        if (!rc) printf("%s: %zu lines\n", dst, lines);
    }
    /* The linker's intermediates beside the output. */
    const char *side[3] = {".link.o", ".link.optimized.o", ".link.symbols.txt"};
    for (int i = 0; i < 3; i++) {
        char *s = fmt("%s/%s%s", dir, p->name, side[i]);
        unlink(s);
        free(s);
    }
    unlink(tmp);
    free(text), free(id), free(bc), free(dir), free(tmp), free(dst);
    return rc;
}

/* The folders of a native program's source identity: the program's, the
 * worker SDK's (packages/mesh-worker*), the contract's (packages/mesh-abi*),
 * the references' (packages/mesh-ref*), and the manifest's `# hash-also:`. */
static size_t hash_dirs(const mbp_opts *o, const program *p, const char *also, char **out, size_t cap) {
    size_t n = 0;
    out[n++] = fmt("%s", p->toml_dir);
    char *pk = fmt("%s/packages", o->root);
    DIR *d = opendir(pk);
    struct dirent *e;
    while (d && (e = readdir(d)) && n < cap)
        if (strncmp(e->d_name, "mesh-worker", 11) == 0 || strncmp(e->d_name, "mesh-abi", 8) == 0 || strncmp(e->d_name, "mesh-ref", 8) == 0)
            out[n++] = fmt("packages/%s", e->d_name);
    if (d) closedir(d);
    free(pk);
    for (const char *a = also; *a && n < cap;) {
        while (*a == ' ' || *a == '\n') a++;
        size_t k = strcspn(a, " \n");
        if (k) out[n++] = fmt("%.*s", (int)k, a);
        a += k;
    }
    return n;
}

/* A native worker program for this machine's target. */
static int build_native(const mbp_opts *o, const program *p) {
    char *dir = fmt("%s/%s", o->out, p->name);
    char also[512] = "", os[64] = "";
    mesh_catalog_toml_comment(p->toml, "hash-also", also, sizeof also);
    /* `# build-os: Darwin`: built on that OS only (elsewhere the catalog gets the manifest without a binary). */
    struct utsname u;
    bool other_os = mesh_catalog_toml_comment(p->toml, "build-os", os, sizeof os) && os[0] && (uname(&u) != 0 || strcmp(os, u.sysname) != 0);
    int rc = 0;
    char *bin = NULL;
    if (!other_os) {
        if (!p->native) { free(dir); return fail("programs/%s has no executable package", p->name); }
        if (sexc_build(o, p->native)) { free(dir); return 1; }
        char *src = fmt("%s/.sexc/out/release/bin/%s", o->root, p->native_bin), *td = fmt("%s/%s", dir, mbp_triple());
        bin = fmt("%s/%s", td, p->name);
        if (!mkdirs(td) || !install(src, bin)) rc = fail("%s: %s", bin, strerror(errno));
        free(src), free(td);
    }
    if (!rc) rc = write_toml(o, p);
    char *dirs[MBP_MAX];
    size_t nd = rc ? 0 : hash_dirs(o, p, also, dirs, MBP_MAX);
    char hash[17];
    if (!rc && !mesh_source_hash(o->root, (const char *const *)dirs, nd, hash)) rc = fail("%s: reading the sources failed", p->toml_dir);
    for (size_t i = 0; i < nd; i++) free(dirs[i]);
    if (!rc) {
        char *sh = fmt("%s/source.hash", dir), *tmp = fmt("%s.tmp", sh);
        FILE *f = fopen(tmp, "w");
        bool ok = f && fprintf(f, "%s\n", hash) > 0;
        if (f) ok = fclose(f) == 0 && ok;
        if (!ok || rename(tmp, sh) != 0) rc = fail("%s: %s", sh, strerror(errno));
        free(sh), free(tmp);
    }
    if (!rc) {
        if (other_os) printf("%s: native, %s only: no binary for %s\n", dir, os, mbp_triple());
        else {
            struct stat st;
            printf("%s: native, %lld bytes, source %s\n", bin, stat(bin, &st) == 0 ? (long long)st.st_size : 0LL, hash);
        }
        rc = variants(o, p);
    }
    free(bin);
    free(dir);
    return rc;
}

int mbp_run(const mbp_opts *o, char *const *names, size_t n) {
    if (!mbp_valid_arch(o->arch)) return fail("MESH_PTX_ARCH=%s: want sm_NN (e.g. sm_86, sm_121)", o->arch);
    static programs ps;
    memset(&ps, 0, sizeof ps);
    int rc = scan(o, &ps);
    char *const *want = names;
    size_t nw = n;
    char *all[MBP_MAX];
    if (!rc && nw == 0) {
        for (size_t i = 0; i < ps.n; i++) all[i] = ps.v[i].name;
        want = all;
        nw = ps.n;
    }
    for (size_t i = 0; i < nw && rc == 0; i++) {
        program *p = NULL;
        for (size_t k = 0; k < ps.n; k++)
            if (strcmp(ps.v[k].name, want[i]) == 0) p = &ps.v[k];
        if (!p || !p->toml) { rc = fail("programs/%s needs program.toml and sexc.json", want[i]); break; }
        if (mesh_catalog_toml_native(p->toml)) {
            if (!o->wasm_only) rc = build_native(o, p);
            continue;
        }
        if (!o->native_only && !o->skip_wasm && mesh_catalog_toml_backend(p->toml, "wasm")) rc = build_wasm(o, p);
        if (rc) break;
        /* A wasm-only program (backends lists neither cuda nor metal) has no PTX. */
        bool gpu = !(mesh_catalog_toml_has_backends(p->toml) && !mesh_catalog_toml_backend(p->toml, "cuda") && !mesh_catalog_toml_backend(p->toml, "metal"));
        if (o->wasm_only || !gpu) {
            rc = write_toml(o, p);
            if (!rc) rc = variants(o, p);
            continue;
        }
        if (o->native_only) continue;
        rc = build_ptx(o, p);
        if (!rc) rc = variants(o, p);
    }
    for (size_t i = 0; i < ps.n; i++) {
        program *p = &ps.v[i];
        free(p->name), free(p->toml_dir), free(p->toml), free(p->gpu), free(p->native), free(p->wasm), free(p->native_bin);
    }
    return rc;
}
