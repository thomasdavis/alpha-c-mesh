/* Rust's float Display ({}) and fixed precision ({:.N}, {:+.2}) for 1800 f32 and f64 values: rf_f32 /
 * rf_f64 and printf must write what rustc wrote. NaN with a precision is the one known difference
 * (Rust "NaN", printf "nan"), checked separately. */
#include "golden.h"
#include "mesh/rust_fmt.h"

#include <math.h>

int main(void) {
    size_t len;
    char *text = slurp("data/floats.txt", &len);
    mt_strs lines = mt_lines(text);
    size_t checked = 0;
    for (size_t i = 0; i < lines.n; i++) {
        mt_strs w = mt_words(lines.v[i]);
        if (w.n != 8) {
            mt_strs_free(&w);
            continue;
        }
        bool is32 = strcmp(w.v[0], "f32") == 0;
        double x = is32 ? (double)f32_bits(w.v[1]) : f64_bits(w.v[1]);
        char d[RF_FLOAT_MAX];
        if (is32) rf_f32(d, (float)x);
        else rf_f64(d, x);
        if (!CHECK_S(d, w.v[2])) fprintf(stderr, "  %s %s\n", w.v[0], w.v[1]);
        if (!isnan(x)) {
            const char *fmts[] = {"%.0f", "%.1f", "%.2f", "%.3f", "%+.2f"};
            for (int k = 0; k < 5; k++) {
                char *s = mt_fmt(fmts[k], x);
                if (!CHECK_S(s, w.v[3 + k])) fprintf(stderr, "  %s %s %s\n", w.v[0], w.v[1], fmts[k]);
                free(s);
            }
        } else CHECK_S(w.v[3], "NaN");
        checked++;
        mt_strs_free(&w);
    }
    CHECK(checked > 3000);
    mt_strs_free(&lines);
    free(text);
    return mesh_check_done();
}
