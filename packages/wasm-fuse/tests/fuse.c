/* What fuses (local.get or a constant into the next plain instruction, two local.gets), what
 * doesn't (across a branch target, into control, an i64 constant into a store), and the remapping
 * of every branch target. */
#include "mesh/check.h"
#include "wasm/fuse.h"

#include <stdlib.h>
#include <string.h>

static wasm_fn_code code(const wasm_ins *ins, uint32_t n) {
    wasm_fn_code c;
    memset(&c, 0, sizeof c);
    c.code = malloc(n * sizeof *ins);
    memcpy(c.code, ins, n * sizeof *ins);
    c.ncode = n;
    return c;
}

int main(void) {
    /* 0 local.get 1; 1 i32.const 5; 2 i32.add; 3 local.get 0; 4 local.get 1; 5 i32.load off 8;
     * 6 BR_IF -> 1; 7 i64.const 9; 8 i64.store off 4; 9 local.get 2; 10 BR -> 9; 11 RETURN */
    wasm_ins in[] = {{0x20, 0, 1, 0}, {0x41, 0, 0, 5}, {0x6a, 0, 0, 0}, {0x20, 0, 0, 0}, {0x20, 0, 1, 0},
                     {0x28, 0, 0, 8}, {WASM_INS_BR_IF, 0, 1, 0}, {0x42, 0, 0, 9}, {0x37, 0, 0, 4},
                     {0x20, 0, 2, 0}, {WASM_INS_BR, 0, 9, 0}, {WASM_INS_RETURN, 0, 0, 0}};
    wasm_fn_code c = code(in, sizeof in / sizeof in[0]);
    c.brs = malloc(sizeof *c.brs);
    c.brs[0] = (wasm_br_entry){7, 0, 0};
    c.nbrs = 1;
    CHECK(wasm_fuse(&c));
    /* local.get 1 | i32.const 5 is a branch target: no fusion across it, but const+add fuse. */
    uint16_t want[] = {0x20, WASM_INS_I + 0x6a, WASM_INS_GET2, 0x28, WASM_INS_BR_IF, 0x42, 0x37, 0x20, WASM_INS_BR,
                       WASM_INS_RETURN};
    CHECK_U(c.ncode, sizeof want / sizeof want[0]);
    for (uint32_t k = 0; k < c.ncode && k < sizeof want / sizeof want[0]; k++) CHECK_U(c.code[k].op, want[k]);
    CHECK_U(c.code[1].b, 5);
    CHECK_U(c.code[2].a, 0);
    CHECK_U(c.code[2].b, 1);
    CHECK_U(c.code[3].b, 8);
    CHECK_U(c.code[4].a, 1); /* still the fused const+add */
    CHECK_U(c.code[8].a, 7); /* local.get 2, a target, kept whole */
    CHECK_U(c.brs[0].target, 5); /* the i64.const */
    free(c.code);
    free(c.brs);
    /* Loads take a local address and an i32.const address; stores a local value. */
    wasm_ins m[] = {{0x20, 0, 3, 0}, {0x28, 0, 0, 16}, {0x41, 0, 0, 64}, {0x29, 0, 0, 8}, {0x20, 0, 4, 0},
                    {0x36, 0, 0, 12}, {WASM_INS_RETURN, 0, 0, 0}};
    c = code(m, 7);
    CHECK(wasm_fuse(&c));
    CHECK_U(c.ncode, 4);
    CHECK_U(c.code[0].op, WASM_INS_L + 0x28);
    CHECK_U(c.code[0].a, 3);
    CHECK_U(c.code[0].b, 16);
    CHECK_U(c.code[1].op, WASM_INS_I + 0x29);
    CHECK_U(c.code[1].a, 64);
    CHECK_U(c.code[1].b, 8);
    CHECK_U(c.code[2].op, WASM_INS_L + 0x36);
    free(c.code);
    CHECK(wasm_fusible(0x6a));
    CHECK(wasm_fusible(0x100));
    CHECK(!wasm_fusible(0x41));
    CHECK(!wasm_fusible(0x3f));
    CHECK(!wasm_fusible(WASM_INS_BR_IF));
    CHECK(!wasm_fusible(0x20));
    return mesh_check_done();
}
