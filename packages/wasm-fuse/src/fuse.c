/* One pass: mark branch targets, fuse pairs, compact, remap. */
#include "wasm/fuse.h"

#include "wasm/opcodes.h"

#include <stdlib.h>
#include <string.h>

bool wasm_fusible(uint16_t op) {
    if (op >= WASM_INS_BR) return false;
    if (op == WASM_OP_I32_CONST || op == WASM_OP_I64_CONST || op == WASM_OP_F32_CONST || op == WASM_OP_F64_CONST) return false;
    if (op == WASM_OP_MEMORY_SIZE || op == WASM_OP_MEMORY_GROW) return false;
    uint32_t id = op >= 0x100 ? WASM_OP_FC(op - 0x100u) : op;
    wasm_opinfo info;
    if (!wasm_op_info(id, &info) || !info.sig) return false;
    /* Something must be popped for an operand to be fused in. */
    return info.sig[0] != '>';
}

static bool is_mem(uint16_t op) { return op >= 0x28 && op <= 0x3e; }
static bool is_const(uint16_t op) {
    return op == WASM_OP_I32_CONST || op == WASM_OP_I64_CONST || op == WASM_OP_F32_CONST || op == WASM_OP_F64_CONST;
}
static bool has_target(uint16_t op) {
    return op == WASM_INS_BR || op == WASM_INS_BR_IF || op == WASM_INS_BR_UNLESS || op == WASM_INS_JUMP;
}

bool wasm_fuse(wasm_fn_code *c) {
    uint32_t n = c->ncode;
    bool *target = calloc(n + 1, sizeof *target);
    uint32_t *map = malloc((n + 1) * sizeof *map);
    wasm_ins *out = malloc((n ? n : 1) * sizeof *out);
    if (!target || !map || !out) {
        free(target);
        free(map);
        free(out);
        return false;
    }
    for (uint32_t k = 0; k < n; k++)
        if (has_target(c->code[k].op)) target[c->code[k].a] = true;
    for (uint32_t k = 0; k < c->nbrs; k++) target[c->brs[k].target] = true;
    uint32_t m = 0;
    for (uint32_t k = 0; k < n;) {
        wasm_ins x = c->code[k];
        map[k] = m;
        if (k + 1 < n && !target[k + 1]) {
            wasm_ins y = c->code[k + 1];
            if (x.op == WASM_OP_LOCAL_GET && wasm_fusible(y.op)) {
                /* The plain op keeps its own b (a load's or store's offset); a is the local. */
                out[m++] = (wasm_ins){(uint16_t)(WASM_INS_L + y.op), 0, x.a, y.b};
                map[k + 1] = m - 1;
                k += 2;
                continue;
            }
            if (is_const(x.op) && wasm_fusible(y.op)) {
                if (!is_mem(y.op)) {
                    out[m++] = (wasm_ins){(uint16_t)(WASM_INS_I + y.op), 0, 0, x.b};
                    map[k + 1] = m - 1;
                    k += 2;
                    continue;
                }
                /* A load's address or a store's value: only when it fits a (i32.const). */
                if (x.op == WASM_OP_I32_CONST) {
                    out[m++] = (wasm_ins){(uint16_t)(WASM_INS_I + y.op), 0, (uint32_t)x.b, y.b};
                    map[k + 1] = m - 1;
                    k += 2;
                    continue;
                }
            }
            if (x.op == WASM_OP_LOCAL_GET && y.op == WASM_OP_LOCAL_GET) {
                out[m++] = (wasm_ins){WASM_INS_GET2, 0, x.a, y.a};
                map[k + 1] = m - 1;
                k += 2;
                continue;
            }
        }
        out[m++] = x;
        k++;
    }
    map[n] = m;
    for (uint32_t k = 0; k < m; k++)
        if (has_target(out[k].op)) out[k].a = map[out[k].a];
    for (uint32_t k = 0; k < c->nbrs; k++) c->brs[k].target = map[c->brs[k].target];
    free(c->code);
    c->code = out;
    c->ncode = m;
    free(target);
    free(map);
    return true;
}
