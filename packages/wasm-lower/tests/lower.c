/* The shape of lowered code: branch targets and heights, the loop's interrupt check, dead code gone. */
#include "mesh/check.h"
#include "wasm/builder.h"
#include "wasm/decode.h"
#include "wasm/lower.h"
#include "wasm/validate.h"

#include <stdlib.h>

static char err[256];

int main(void) {
    wasm_builder w = {0};
    uint32_t t = wasm_builder_type(&w, "i>i");
    /* block (result i32) i32.const 7 local.get 0 br_if 0 drop i32.const 9 end drop;
     * block loop local.get 0 eqz br_if 0 br 1 (dead: i32.const 1 drop) end end i32.const 3 */
    wasm_buf *b = wasm_builder_func_add(&w, t, "l", NULL);
    static const uint8_t body[] = {0x02, 0x7f, 0x41, 7, 0x20, 0, 0x0d, 0, 0x1a, 0x41, 9, 0x0b, 0x1a, 0x02, 0x40,
                                   0x03, 0x40, 0x20, 0, 0x45, 0x0d, 0, 0x0c, 1, 0x41, 1, 0x1a, 0x0b, 0x0b, 0x41, 3};
    wb_bytes(b, body, sizeof body);
    wasm_buf out;
    wasm_builder_finish(&w, &out);
    wasm_module m;
    CHECK(wasm_decode(out.data, out.len, &m, err, sizeof err));
    CHECK(wasm_validate(&m, WASM_F_ALL, NULL, err, sizeof err));
    wasm_fn_code c;
    CHECK(wasm_lower(&m, 0, &c, err, sizeof err));
    CHECK_U(c.nlocals, 2);
    /* i32.const 7, local.get 0, br_if (to the block's end: height 2 = 2 locals + 0, 1 value), drop,
     * i32.const 9, drop, INTERRUPT, local.get, eqz, br_if (to the loop: its INTERRUPT), br (out:
     * dead code after it is gone), i32.const 3, return. */
    uint16_t want[] = {0x41, 0x20, WASM_INS_BR_IF, 0x1a, 0x41, 0x1a, WASM_INS_INTERRUPT, 0x20, 0x45, WASM_INS_BR_IF,
                       WASM_INS_BR, 0x41, WASM_INS_RETURN};
    CHECK_U(c.ncode, sizeof want / sizeof want[0]);
    for (uint32_t i = 0; i < c.ncode && i < sizeof want / sizeof want[0]; i++) CHECK_U(c.code[i].op, want[i]);
    CHECK_U(c.code[2].a, 5);            /* past the block's last instruction (i32.const 9 is 4) */
    CHECK_U(c.code[2].b, (2ull << 32) | 1);
    CHECK_U(c.code[9].a, 6);            /* the loop's header */
    CHECK_U(c.code[9].b, (2ull << 32) | 0);
    CHECK_U(c.code[10].a, 11);          /* out of the loop */
    CHECK_U(c.code[12].a, 1);
    CHECK(c.frame_slots >= 2 + 2);
    wasm_fn_code_free(&c);
    uint32_t *canon = wasm_canonical_types(&m);
    CHECK_U(canon[0], 0);
    free(canon);
    wasm_module_free(&m);
    wasm_buf_free(&out);
    wasm_builder_free(&w);
    return mesh_check_done();
}
