/* 500 random Needs from the Rust: decode(encode) re-encodes to the same token, and the summary matches. */
#include "golden.h"
#include "mesh/sched_needs.h"

int main(void) {
    size_t len;
    char *text = slurp("data/needs.txt", &len);
    mt_strs lines = mt_lines(text);
    for (size_t i = 0; i < lines.n; i++) {
        mt_strs w = mt_words(lines.v[i]);
        msn_needs n;
        char *err = NULL;
        if (!CHECK(w.n == 2 && msn_decode(w.v[0], &n, &err))) {
            fprintf(stderr, "line %zu: %s\n", i + 1, err ? err : "?");
            free(err);
            mt_strs_free(&w);
            continue;
        }
        char *e = msn_encode(&n), *s = msn_summary(&n), *want = unhex(w.v[1]);
        CHECK_S(e, w.v[0]);
        CHECK_S(s, want);
        free(e), free(s), free(want);
        msn_free(&n);
        mt_strs_free(&w);
    }
    CHECK_U(lines.n, 500);
    mt_strs_free(&lines);
    free(text);
    return mesh_check_done();
}
