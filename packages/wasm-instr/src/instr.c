/* Decoding one instruction: opcode, sub-opcode, immediates. */
#include "wasm/instr.h"

#include <string.h>

static void memarg(wasm_reader *r, wasm_instr *in) {
    in->align = wasm_rd_u32(r);
    in->mem = 0;
    /* Multi-memory: bit 6 of the alignment says a memory index follows. */
    if (in->align & 0x40) {
        in->align &= ~0x40u;
        in->mem = wasm_rd_u32(r);
    }
    in->moffset = wasm_rd_u64(r);
}

static void blocktype(wasm_reader *r, wasm_instr *in) {
    size_t at = wasm_rd_offset(r);
    int64_t bt = wasm_rd_s33(r);
    in->blocktype = bt;
    in->bt_val = 0;
    if (bt >= 0 || bt == WASM_BT_EMPTY) return;
    /* A negative one-byte s33 is a value type: 0x7f is -1, 0x6f is -17. */
    if (bt < -64) {
        wasm_rd_fail_at(r, at, "invalid block type");
        return;
    }
    wasm_valtype t = (wasm_valtype)(bt + 0x80);
    if (!wasm_is_valtype(t)) {
        wasm_rd_fail_at(r, at, "invalid value type");
        return;
    }
    in->bt_val = t;
}

static wasm_valtype reftype(wasm_reader *r) {
    size_t at = wasm_rd_offset(r);
    uint8_t t = wasm_rd_u8(r);
    if (!r->failed && t != WASM_FUNCREF && t != WASM_EXTERNREF) wasm_rd_fail_at(r, at, "malformed reference type");
    return t;
}

bool wasm_rd_instr(wasm_reader *r, wasm_instr *in) {
    memset(in, 0, sizeof *in);
    in->offset = wasm_rd_offset(r);
    uint32_t op = wasm_rd_u8(r);
    if (r->failed) return false;
    if (op == WASM_PREFIX_FC || op == WASM_PREFIX_SIMD || op == WASM_PREFIX_ATOMIC) {
        uint32_t sub = wasm_rd_u32(r);
        if (r->failed) return false;
        if (sub > 0xffff) return wasm_rd_fail_at(r, in->offset, "unknown 0x%02x subopcode: 0x%x", op, sub);
        op = op << 16 | sub;
    }
    in->op = op;
    if (!wasm_op_info(op, &in->info)) {
        if (op > 0xff) return wasm_rd_fail_at(r, in->offset, "unknown 0x%02x subopcode: 0x%x", op >> 16, op & 0xffff);
        return wasm_rd_fail_at(r, in->offset, "illegal opcode: 0x%x", op);
    }
    switch (in->info.imm) {
    case WASM_IMM_NONE: break;
    case WASM_IMM_BLOCK: blocktype(r, in); break;
    case WASM_IMM_LABEL:
    case WASM_IMM_FUNC:
    case WASM_IMM_LOCAL:
    case WASM_IMM_GLOBAL:
    case WASM_IMM_TABLE:
    case WASM_IMM_DATA:
    case WASM_IMM_ELEM: in->a = wasm_rd_u32(r); break;
    case WASM_IMM_MEMIDX: in->mem = wasm_rd_u32(r); break;
    case WASM_IMM_BR_TABLE: {
        in->ntargets = wasm_rd_count(r, 1, "br_table");
        in->targets = r->p;
        for (uint32_t i = 0; i < in->ntargets && !r->failed; i++) (void)wasm_rd_u32(r);
        in->b = wasm_rd_u32(r); /* the default */
        break;
    }
    case WASM_IMM_CALL_INDIRECT:
        in->a = wasm_rd_u32(r); /* type */
        in->b = wasm_rd_u32(r); /* table */
        break;
    case WASM_IMM_MEMARG: memarg(r, in); break;
    case WASM_IMM_MEMARG_LANE:
        memarg(r, in);
        in->lane = wasm_rd_u8(r);
        break;
    case WASM_IMM_I32: in->i = wasm_rd_s32(r); break;
    case WASM_IMM_I64: in->i = wasm_rd_s64(r); break;
    case WASM_IMM_F32: in->f32 = wasm_rd_le32(r); break;
    case WASM_IMM_F64: in->f64 = wasm_rd_le64(r); break;
    case WASM_IMM_SELECT_T: {
        size_t at = wasm_rd_offset(r);
        in->nselect = wasm_rd_count(r, 1, "select types");
        for (uint32_t i = 0; i < in->nselect && !r->failed; i++) {
            size_t tat = wasm_rd_offset(r);
            uint8_t t = wasm_rd_u8(r);
            if (!r->failed && !wasm_is_valtype(t)) return wasm_rd_fail_at(r, tat, "invalid value type");
            if (i == 0) in->select_type = t;
        }
        if (!r->failed && in->nselect != 1) return wasm_rd_fail_at(r, at, "invalid result arity");
        break;
    }
    case WASM_IMM_REFTYPE: in->reftype = reftype(r); break;
    case WASM_IMM_MEMORY_INIT:
        in->a = wasm_rd_u32(r); /* data */
        in->mem = wasm_rd_u32(r);
        break;
    case WASM_IMM_MEMORY_COPY:
        in->mem = wasm_rd_u32(r); /* destination */
        in->b = wasm_rd_u32(r);   /* source */
        break;
    case WASM_IMM_TABLE_INIT:
        in->a = wasm_rd_u32(r); /* element */
        in->b = wasm_rd_u32(r); /* table */
        break;
    case WASM_IMM_TABLE_COPY:
        in->a = wasm_rd_u32(r); /* destination */
        in->b = wasm_rd_u32(r); /* source */
        break;
    case WASM_IMM_V128:
    case WASM_IMM_SHUFFLE: in->v128 = wasm_rd_bytes(r, 16); break;
    case WASM_IMM_LANE: in->lane = wasm_rd_u8(r); break;
    case WASM_IMM_ZERO_BYTE: {
        size_t at = wasm_rd_offset(r);
        if (wasm_rd_u8(r) != 0 && !r->failed) return wasm_rd_fail_at(r, at, "nonzero byte after atomic.fence");
        break;
    }
    }
    return !r->failed;
}

void wasm_instr_targets(const wasm_instr *in, uint32_t *out) {
    const uint8_t *p = in->targets;
    for (uint32_t i = 0; i < in->ntargets; i++) {
        uint32_t v = 0;
        /* Already checked by wasm_rd_instr: these can't fail; bound by 5 bytes each. */
        p += wasm_leb_u32(p, p + 5, &v);
        out[i] = v;
    }
    out[in->ntargets] = in->b;
}
