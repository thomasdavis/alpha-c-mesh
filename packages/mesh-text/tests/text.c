/* mesh-text against Rust 1.88: {:?} of f64, f32 and str, and str::trim (vectors printed by the Rust). */
#include "mesh/check.h"
#include "mesh/text.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void f64_is(double v, const char *want) {
    mesh_text t = {0};
    mesh_text_debug_f64(&t, v);
    CHECK_S(mesh_text_str(&t), want);
    mesh_text_free(&t);
}

static void f32_is(float v, const char *want) {
    mesh_text t = {0};
    mesh_text_debug_f32(&t, v);
    CHECK_S(mesh_text_str(&t), want);
    mesh_text_free(&t);
}

static void str_is(const char *s, size_t n, const char *want) {
    mesh_text t = {0};
    mesh_text_debug_str(&t, s, n);
    CHECK_S(mesh_text_str(&t), want);
    mesh_text_free(&t);
}

static void trim_is(const char *s, const char *want) {
    size_t n = strlen(s);
    mesh_trim(&s, &n);
    char *got = mesh_strndup(s, n);
    CHECK_S(got, want);
    free(got);
}

int main(void) {
    f64_is(0.0, "0.0");
    f64_is(-0.0, "-0.0");
    f64_is(1.0, "1.0");
    f64_is(2.0, "2.0");
    f64_is(0.5, "0.5");
    f64_is(0.1, "0.1");
    f64_is(0.2, "0.2");
    f64_is(0.3, "0.3");
    f64_is(1.0/3.0, "0.3333333333333333");
    f64_is(2.0/3.0, "0.6666666666666666");
    f64_is(123.456, "123.456");
    f64_is(1e15, "1000000000000000.0");
    f64_is(1e16, "1e16");
    f64_is(1e17, "1e17");
    f64_is(1.5e16, "1.5e16");
    f64_is(9.999999999999998e15, "9999999999999998.0");
    f64_is(1e-4, "0.0001");
    f64_is(1e-5, "1e-5");
    f64_is(1.5e-7, "1.5e-7");
    f64_is(0.00012, "0.00012");
    f64_is(5e-324, "5e-324");
    f64_is(1.7976931348623157e308, "1.7976931348623157e308");
    f64_is(NAN, "NaN");
    f64_is(INFINITY, "inf");
    f64_is(-INFINITY, "-inf");
    f64_is(100.0, "100.0");
    f64_is(1024.0, "1024.0");
    f64_is(0.01, "0.01");
    f64_is(2.5, "2.5");
    f64_is(1234567.0, "1234567.0");
    f64_is(0.000123456, "0.000123456");
    f64_is(3.14159, "3.14159");
    f64_is(-2.75, "-2.75");
    f64_is(4096.0, "4096.0");
    f64_is(1e300, "1e300");
    f64_is(1e-300, "1e-300");
    f64_is(12345678901234567.0, "1.2345678901234568e16");
    f32_is(0.1f, "0.1");
    f32_is(1.0f/3.0f, "0.33333334");
    f32_is(16777216.0f, "16777216.0");
    f32_is(1e-7f, "1e-7");
    f32_is(3.4028235e38f, "3.4028235e38");
    f32_is(1.0e10f, "10000000000.0");
    f32_is(0.3f, "0.3");
    str_is("", 0, "\"\"");
    str_is("abc", 3, "\"abc\"");
    str_is("a\"b", 3, "\"a\\\"b\"");
    str_is("back\\slash", 10, "\"back\\\\slash\"");
    str_is("nl\012x", 4, "\"nl\\nx\"");
    str_is("tab\011", 4, "\"tab\\t\"");
    str_is("cr\015", 3, "\"cr\\r\"");
    str_is("nul\0x", 5, "\"nul\\0x\"");
    str_is("\033[0m", 4, "\"\\u{1b}[0m\"");
    str_is("\177", 1, "\"\\u{7f}\"");
    str_is("\303\251 \303\274", 5, "\"\303\251 \303\274\"");
    str_is("\302\205", 2, "\"\\u{85}\"");
    str_is("\302\240x", 3, "\"\\u{a0}x\"");
    str_is("'single'", 8, "\"'single'\"");
    str_is("\342\200\213", 3, "\"\\u{200b}\"");
    str_is("\346\227\245\346\234\254", 6, "\"\346\227\245\346\234\254\"");
    trim_is(" a ", "a");
    trim_is("\302\240b\343\200\200", "b");
    trim_is("\011\012 x \015\012", "x");
    trim_is("\342\200\250y\302\205", "y");
    trim_is("z\357\273\277", "z\357\273\277");
    /* Parsing as Rust's from_str. */
    uint32_t u;
    CHECK(mesh_parse_u32("+5", 2, 10, &u) && u == 5);
    CHECK(!mesh_parse_u32("-5", 2, 10, &u));
    CHECK(!mesh_parse_u32("", 0, 10, &u));
    CHECK(!mesh_parse_u32("4294967296", 10, 10, &u));
    CHECK(mesh_parse_u32("ffffffff", 8, 16, &u) && u == 0xffffffffu);
    CHECK(!mesh_parse_u32("1 ", 2, 10, &u));
    double d;
    CHECK(mesh_parse_f64("2.0", 3, &d) && d == 2.0);
    CHECK(mesh_parse_f64(".5", 2, &d) && d == 0.5);
    CHECK(mesh_parse_f64("1.", 2, &d) && d == 1.0);
    CHECK(mesh_parse_f64("-1e3", 4, &d) && d == -1000.0);
    CHECK(mesh_parse_f64("inf", 3, &d) && isinf(d));
    CHECK(mesh_parse_f64("NaN", 3, &d) && isnan(d));
    CHECK(!mesh_parse_f64("0x1.8p3", 7, &d));
    CHECK(!mesh_parse_f64(" 1.0", 4, &d));
    CHECK(!mesh_parse_f64("1e", 2, &d));
    CHECK(!mesh_parse_f64(".", 1, &d));
    CHECK(!mesh_parse_f64("1_0.0", 5, &d));
    /* Errors and buffers. */
    char *err = NULL;
    CHECK(!mesh_errf(&err, "x %d", 1));
    mesh_err_prefix(&err, "manifest: ");
    CHECK_S(err, "manifest: x 1");
    free(err);
    char *q = mesh_debug_str("say \"hi\"");
    CHECK_S(q, "\"say \\\"hi\\\"\"");
    free(q);
    return mesh_check_done();
}
