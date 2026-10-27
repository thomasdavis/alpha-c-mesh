/* The Rust's `float_guard` test (mesh-host/src/wasm.rs), case for case. */
#include "mesh/check.h"
#include "wasm/builder.h"
#include "wasm/float_guard.h"

#include <string.h>

/* A module of one function of type (f32, f32) -> f32 (or () -> () with simd), exported as export,
 * whose body is body; optionally one function import first (the Rust test's `module`). */
static wasm_buf module(const char *imod, const char *iname, const char *export, const uint8_t *body, size_t n, bool simd) {
    wasm_builder w = {0};
    uint32_t t = wasm_builder_type(&w, simd ? ">" : "ff>f");
    if (imod) wasm_builder_import_func(&w, imod, iname, t);
    uint32_t f;
    wb_bytes(wasm_builder_func_add(&w, t, "", &f), body, n);
    wasm_builder_add_export(&w, export, WASM_EXTERN_FUNC, f);
    wasm_buf out;
    wasm_builder_finish(&w, &out);
    wasm_builder_free(&w);
    return out;
}

static const uint8_t ADD[] = {0x20, 0, 0x20, 1, 0x92}; /* local.get 0, local.get 1, f32.add */
static const uint8_t MOVE[] = {0x20, 0};               /* local.get 0 (no f32 op) */

static bool prefix(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

int main(void) {
    char err[512];
    wasm_float_report r;
    /* f32.add in a non-helper: refused in an FTZ image, fine in an IEEE one. */
    wasm_buf raw = module(NULL, NULL, "f", ADD, sizeof ADD, false);
    CHECK(!wasm_float_check(raw.data, raw.len, true, &r, err, sizeof err));
    CHECK(prefix(err, "float: F32Add in function 0"));
    CHECK(wasm_float_check(raw.data, raw.len, false, &r, err, sizeof err));
    CHECK_U(r.funcs, 1);
    CHECK_U(r.helpers, 0);
    CHECK_U(r.f32_ops, 1);
    wasm_buf_free(&raw);
    /* The same op inside an exported mesh_ftz_* helper: allowed. */
    wasm_buf helper = module(NULL, NULL, "mesh_ftz_add", ADD, sizeof ADD, false);
    CHECK(wasm_float_check(helper.data, helper.len, true, &r, err, sizeof err));
    CHECK_U(r.funcs, 1);
    CHECK_U(r.helpers, 1);
    CHECK_U(r.f32_ops, 1);
    wasm_buf_free(&helper);
    /* Moves are not flushing ops. */
    wasm_buf mv = module(NULL, NULL, "f", MOVE, sizeof MOVE, false);
    CHECK(wasm_float_check(mv.data, mv.len, true, &r, err, sizeof err));
    CHECK_U(r.f32_ops, 0);
    wasm_buf_free(&mv);
    /* Imports: only `mesh` functions (the helper index counts the import). */
    wasm_buf env = module("env", "x", "mesh_ftz_add", ADD, sizeof ADD, false);
    CHECK(!wasm_float_check(env.data, env.len, true, &r, err, sizeof err));
    CHECK(prefix(err, "load: import env::x"));
    wasm_buf_free(&env);
    wasm_buf mesh = module("mesh", "info", "mesh_ftz_add", ADD, sizeof ADD, false);
    CHECK(wasm_float_check(mesh.data, mesh.len, true, &r, err, sizeof err));
    CHECK_U(r.helpers, 1);
    wasm_buf_free(&mesh);
    /* SIMD (v128.const; drop): refused in an FTZ image only. */
    uint8_t simd[19] = {0xfd, 0x0c};
    simd[18] = 0x1a;
    wasm_buf v = module(NULL, NULL, "f", simd, sizeof simd, true);
    CHECK(!wasm_float_check(v.data, v.len, true, &r, err, sizeof err));
    CHECK(prefix(err, "float: "));
    CHECK(wasm_float_check(v.data, v.len, false, &r, err, sizeof err));
    wasm_buf_free(&v);
    /* Not wasm at all: the compiler's refusal. */
    CHECK(!wasm_float_check((const uint8_t *)"\0asm\x01\0\0\0\x01\x05", 10, true, &r, err, sizeof err));
    CHECK(prefix(err, "jit: "));
    /* Threads and relaxed SIMD are refused even in an IEEE image. */
    static const uint8_t fence[] = {0xfe, 0x03, 0x00};
    wasm_buf th = module(NULL, NULL, "f", fence, sizeof fence, true);
    CHECK(!wasm_float_check(th.data, th.len, false, &r, err, sizeof err));
    CHECK(prefix(err, "float: threads support is not enabled"));
    wasm_buf_free(&th);
    /* Every flushing op is one of the 22 the Rust lists. */
    int n = 0;
    for (uint32_t op = 0; op < 256; op++) n += wasm_flushing_f32(op);
    CHECK_I(n, 22);
    return mesh_check_done();
}
