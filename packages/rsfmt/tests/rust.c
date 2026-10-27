/* Against what Rust's format! printed for the same values (generated with the Rust toolchain, see
 * docs/port/top.md): {:.0} {:.1} {:.2} {:.3e} {:+.6} {:.6} {:.0e} {:.2e} {:02.0} {:>3.0}, and padding. */
#include "mesh/check.h"
#include "rsfmt.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static char buf[2048];

static const char *row32(float x) {
    char a[8][128];
    rsfmt_fixed(a[0], 128, x, 0, 0);
    rsfmt_fixed(a[1], 128, x, 1, 0);
    rsfmt_fixed(a[2], 128, x, 2, 0);
    rsfmt_exp(a[3], 128, x, 3, 0);
    rsfmt_fixed(a[4], 128, x, 6, RSFMT_PLUS);
    rsfmt_fixed(a[5], 128, x, 6, 0);
    rsfmt_exp(a[6], 128, (double)x, 0, 0);
    rsfmt_exp(a[7], 128, (double)x * 3.7, 2, 0);
    snprintf(buf, sizeof buf, "%s|%s|%s|%s|%s|%s|%s|%s", a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    return buf;
}

static const char *row64(double x) {
    char a[7][128];
    rsfmt_fixed(a[0], 128, x, 0, 0);
    rsfmt_fixed(a[1], 128, x, 1, 0);
    rsfmt_fixed(a[2], 128, x, 2, 0);
    rsfmt_exp(a[3], 128, x, 0, 0);
    rsfmt_exp(a[4], 128, x, 3, 0);
    rsfmt_fixed_w(a[5], 128, x, 0, 2, '>', RSFMT_ZERO);
    rsfmt_fixed_w(a[6], 128, x, 0, 3, '>', 0);
    snprintf(buf, sizeof buf, "%s|%s|%s|%s|%s|%s|%s", a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
    return buf;
}

int main(void) {
    const float f32s[16] = {0.0f, -0.0f, 0.25f, 0.35f, 1.5f, 2.5f, 12.5f, 0.0049999f, 999.95f, 1e6f, 3.0e-7f,
                            NAN, INFINITY, -INFINITY, 0.1f, 123456.789f};
    const char *want32[16] = {
        "0|0.0|0.00|0.000e0|+0.000000|0.000000|0e0|0.00e0",
        "-0|-0.0|-0.00|-0.000e0|-0.000000|-0.000000|-0e0|-0.00e0",
        "0|0.2|0.25|2.500e-1|+0.250000|0.250000|2e-1|9.25e-1",
        "0|0.3|0.35|3.500e-1|+0.350000|0.350000|3e-1|1.29e0",
        "2|1.5|1.50|1.500e0|+1.500000|1.500000|2e0|5.55e0",
        "2|2.5|2.50|2.500e0|+2.500000|2.500000|2e0|9.25e0",
        "12|12.5|12.50|1.250e1|+12.500000|12.500000|1e1|4.62e1",
        "0|0.0|0.00|5.000e-3|+0.005000|0.005000|5e-3|1.85e-2",
        "1000|1000.0|999.95|1.000e3|+999.950012|999.950012|1e3|3.70e3",
        "1000000|1000000.0|1000000.00|1.000e6|+1000000.000000|1000000.000000|1e6|3.70e6",
        "0|0.0|0.00|3.000e-7|+0.000000|0.000000|3e-7|1.11e-6",
        "NaN|NaN|NaN|NaN|NaN|NaN|NaN|NaN",
        "inf|inf|inf|inf|+inf|inf|inf|inf",
        "-inf|-inf|-inf|-inf|-inf|-inf|-inf|-inf",
        "0|0.1|0.10|1.000e-1|+0.100000|0.100000|1e-1|3.70e-1",
        "123457|123456.8|123456.79|1.235e5|+123456.789062|123456.789062|1e5|4.57e5",
    };
    for (int i = 0; i < 16; i++) CHECK_S(row32(f32s[i]), want32[i]);
    const double f64s[12] = {0.0, 0.5, 1.5, 2.5, 0.05, 0.15, 1e-3, 3.16227766e-2, 99.95, 1e21, 5e-324, 0.000123456};
    const char *want64[12] = {
        "0|0.0|0.00|0e0|0.000e0|00|  0",
        "0|0.5|0.50|5e-1|5.000e-1|00|  0",
        "2|1.5|1.50|2e0|1.500e0|02|  2",
        "2|2.5|2.50|2e0|2.500e0|02|  2",
        "0|0.1|0.05|5e-2|5.000e-2|00|  0",
        "0|0.1|0.15|1e-1|1.500e-1|00|  0",
        "0|0.0|0.00|1e-3|1.000e-3|00|  0",
        "0|0.0|0.03|3e-2|3.162e-2|00|  0",
        "100|100.0|99.95|1e2|9.995e1|100|100",
        "1000000000000000000000|1000000000000000000000.0|1000000000000000000000.00|1e21|1.000e21|1000000000000000000000|1000000000000000000000",
        "0|0.0|0.00|5e-324|4.941e-324|00|  0",
        "0|0.0|0.00|1e-4|1.235e-4|00|  0",
    };
    for (int i = 0; i < 12; i++) CHECK_S(row64(f64s[i]), want64[i]);
    /* Padding counts characters: "µ" is one. */
    rsfmt_pad(buf, sizeof buf, "p50 µs", 8, '<', ' ');
    CHECK_S(buf, "p50 µs  ");
    rsfmt_pad(buf, sizeof buf, "ab", 5, '^', ' ');
    CHECK_S(buf, " ab  ");
    rsfmt_pad(buf, sizeof buf, "abcdef", 3, '>', ' ');
    CHECK_S(buf, "abcdef");
    rsfmt_fixed_w(buf, sizeof buf, -5.0, 0, 3, '>', RSFMT_ZERO);
    CHECK_S(buf, "-05");
    CHECK_I(rsfmt_fixed(buf, 4, 123.456, 2, 0), 6);
    CHECK_S(buf, "123");
    CHECK_U(rsfmt_chars("→1 µs"), 5);
    CHECK_U(rsfmt_prefix("→1 µs", 2), 4);
    return mesh_check_done();
}
