/* float_check, as in mesh-host/src/wasm.rs: validate twice (every proposal, then without what the
 * executor never runs), then walk imports, exports and every operator. */
#include "wasm/float_guard.h"

#include "mesh/abi/wasm.h"
#include "wasm/decode.h"
#include "wasm/instr.h"
#include "wasm/validate.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool wasm_flushing_f32(uint32_t op) {
    switch (op) {
    case 0x8b: case 0x8c: case 0x8d: case 0x8e: case 0x8f: case 0x90: case 0x91: /* abs neg ceil floor trunc nearest sqrt */
    case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: case 0x98: /* add sub mul div min max copysign */
    case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: case 0x60:             /* eq ne lt gt le ge */
    case 0xb6: case 0xbb:                                                         /* demote, promote */
        return true;
    default: return false;
    }
}

static bool say(char *err, size_t errlen, const char *fmt, ...) {
    if (err && errlen) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, errlen, fmt, ap);
        va_end(ap);
    }
    return false;
}

static bool starts_with(const uint8_t *s, uint32_t n, const char *prefix) {
    size_t k = strlen(prefix);
    return n >= k && memcmp(s, prefix, k) == 0;
}

bool wasm_float_check(const uint8_t *wasm, size_t len, bool ftz, wasm_float_report *rep, char *err, size_t errlen) {
    char msg[384];
    wasm_module m;
    wasm_float_report r = {0};
    /* Well-formed under every proposal we know (else it is the compiler's refusal, `jit`) ... */
    if (!wasm_decode(wasm, len, &m, msg, sizeof msg)) return say(err, errlen, "jit: invalid wasm: %s", msg);
    bool ok = false;
    bool *helpers = NULL;
    if (!wasm_validate(&m, WASM_F_ALL, NULL, msg, sizeof msg)) {
        say(err, errlen, "jit: invalid wasm: %s", msg);
        goto out;
    }
    /* ... and without what this executor never runs: threads, shared memory and relaxed SIMD
     * anywhere; any SIMD in an FTZ image (its f32x4 ops have no flush). */
    wasm_features f = WASM_F_ALL & ~(wasm_features)(WASM_F_THREADS | WASM_F_RELAXED_SIMD);
    if (ftz) f &= ~(wasm_features)WASM_F_SIMD;
    if (!wasm_validate(&m, f, NULL, msg, sizeof msg)) {
        say(err, errlen, "float: %s (a CPU program uses no threads, shared memory or relaxed SIMD%s)", msg,
            ftz ? "; an FTZ image no SIMD" : "");
        goto out;
    }
    for (uint32_t i = 0; i < m.nimports; i++) {
        const wasm_import *im = &m.imports[i];
        if (!(im->module_len == strlen(MESH_WASM_MODULE) && memcmp(im->module, MESH_WASM_MODULE, im->module_len) == 0) ||
            im->kind != WASM_EXTERN_FUNC) {
            say(err, errlen, "load: import %.*s::%.*s: a CPU program imports only `%s` functions (mesh-abi v2::wasm)",
                (int)im->module_len, im->module, (int)im->name_len, im->name, MESH_WASM_MODULE);
            goto out;
        }
    }
    if (!(helpers = calloc(m.nfuncs ? m.nfuncs : 1, sizeof *helpers))) {
        say(err, errlen, "load: out of memory");
        goto out;
    }
    for (uint32_t i = 0; i < m.nexports; i++) {
        const wasm_export *e = &m.exports[i];
        if (e->kind == WASM_EXTERN_FUNC && starts_with(e->name, e->name_len, MESH_WASM_FTZ_PREFIX) && !helpers[e->index]) {
            helpers[e->index] = true;
            r.helpers++;
        }
    }
    for (uint32_t b = 0; b < m.ncodes; b++) {
        uint32_t func = m.nfunc_imports + b;
        const wasm_expr *body = &m.codes[b].body;
        wasm_reader rd = wasm_reader_at(m.bytes, body->start, body->start + body->len, msg, sizeof msg);
        wasm_instr in;
        while (!wasm_rd_eof(&rd)) {
            if (!wasm_rd_instr(&rd, &in)) {
                say(err, errlen, "jit: %s", msg);
                goto out;
            }
            if (!wasm_flushing_f32(in.op)) continue;
            r.f32_ops++;
            if (ftz && !helpers[func]) {
                say(err, errlen,
                    "float: %s in function %u, outside the image's %s* helpers: an FTZ image (manifest float = \"ftz\", "
                    "the default) computes every f32 op through mesh-ref's Z (crates/mesh-rt-wasm); declare float = "
                    "\"ieee\" for plain f32 (not lockstep)",
                    in.info.name, func, MESH_WASM_FTZ_PREFIX);
                goto out;
            }
        }
    }
    r.funcs = m.ncodes;
    if (rep) *rep = r;
    ok = true;
out:
    free(helpers);
    wasm_module_free(&m);
    return ok;
}
