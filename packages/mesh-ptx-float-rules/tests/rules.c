/* Every rule of the cluster float mode, on lines as ptxas sees them. */
#include "mesh/check.h"
#include "mesh/ptx_float_rules.h"

static const char *why(const char *l) { return mesh_ptx_float_line(l, strlen(l)); }
#define OK(l) CHECK(why(l) == NULL)
#define BAD(l, w) CHECK(why(l) != NULL && strcmp(why(l), w) == 0)

int main(void) {
    /* Allowed. */
    OK("\tadd.rn.ftz.f32 \t%r14, %r12, %r13;");
    OK("\tmul.rn.f64 \t%fd1, %fd2, %fd3;");
    OK("\tdiv.rn.ftz.f32 %r1, %r2, %r3;");
    OK("\tsqrt.rn.ftz.f32 %r1, %r2;");
    OK("\tsetp.lt.ftz.f32 %p1, %r1, %r2;");
    OK("\tsetp.lt.f64 %p1, %fd1, %fd2;");
    OK("\tabs.ftz.f32 %r1, %r2;");
    OK("\tcvt.rn.f32.u32 %r1, %r2;");
    OK("\tcvt.rzi.s32.f64 %r1, %fd2;");
    OK("\tcvt.rn.ftz.f32.f64 %r1, %fd1;");
    OK("\tcvt.ftz.f64.f32 %fd1, %r1;");
    OK("\tmov.b32 %r1, 0f3F800000;");
    OK("\tld.global.f32 %r1, [%rd1];");
    OK("\tselp.f32 %r1, %r2, %r3, %p1;");
    OK("\tadd.s32 %r1, %r2, %r3;"); /* not a float op */
    OK("// add.f32 in a comment");
    OK("\t@%p1 bra $L__BB0_2;");
    OK("\t@!%p2 add.rn.ftz.f32 %r1, %r2, %r3;");
    OK(".visible .entry mesh_main(");
    OK("{ add.rn.ftz.f32 %r1, %r2, %r3; }");
    /* Refused. */
    BAD("\tadd.f32 %r1, %r2, %r3;", "no .rn (ptxas may fuse it, per arch)");
    BAD("\tadd.rn.f32 %r1, %r2, %r3;", "f32 without .ftz");
    BAD("\t@%p1 mul.rn.f32 %r1, %r2, %r3;", "f32 without .ftz");
    BAD("\tfma.rn.ftz.f32 %r1, %r2, %r3, %r4;", "fused multiply-add");
    BAD("\tmad.rn.f64 %fd1, %fd2, %fd3, %fd4;", "fused multiply-add");
    BAD("\tdiv.full.f32 %r1, %r2, %r3;", "div.full");
    BAD("\tsqrt.approx.ftz.f32 %r1, %r2;", ".approx");
    BAD("\tex2.approx.ftz.f32 %r1, %r2;", ".approx");
    BAD("\tsin.ftz.f32 %r1, %r2;", "float instruction with no rule (approximate or not covered)");
    BAD("\tadd.rn.f16 %rs1, %rs2, %rs3;", "16-bit / 8-bit float");
    BAD("\tcvt.rn.f32.f64 %r1, %fd1;", "f32 convert without .ftz");
    BAD("\tcvt.f64.f32 %fd1, %r1;", "f32 convert without .ftz");
    BAD("\tmax.f32 %r1, %r2, %r3;", "f32 without .ftz");
    /* A whole text: the count stops at 5. */
    const char *text = "add.f32 a;\nadd.f32 b;\nmov.b32 c;\nadd.f32 d;\nadd.f32 e;\nadd.f32 f;\nadd.f32 g;\n";
    CHECK_I(mesh_ptx_float_check("x.ptx", text, strlen(text), NULL), 5);
    const char *good = ".version 7.1\n.target sm_86\n\tadd.rn.ftz.f32 %r1, %r2, %r3;\n";
    CHECK_I(mesh_ptx_float_check("y.ptx", good, strlen(good), NULL), 0);
    return mesh_check_done();
}
