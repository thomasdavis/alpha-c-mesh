/* Splitting as Rust splits; buffers and lists. */
#include "mesh/check.h"
#include "mesh/mt.h"

#include <stdlib.h>

int main(void) {
    mt_strs l = mt_split("a,,b", ',');
    CHECK_U(l.n, 3);
    CHECK_S(l.v[1], "");
    mt_strs_free(&l);
    l = mt_split("", ',');
    CHECK_U(l.n, 1);
    CHECK_S(l.v[0], "");
    mt_strs_free(&l);
    l = mt_words("  a b\t\tc\n");
    CHECK_U(l.n, 3);
    CHECK_S(l.v[2], "c");
    char *j = mt_join(&l, ",");
    CHECK_S(j, "a,b,c");
    free(j);
    mt_strs c = mt_strs_clone(&l);
    CHECK(mt_strs_eq(&c, &l) && mt_strs_has(&c, "b") && mt_strs_find(&c, "z") == -1);
    mt_strs_remove(&c, 0);
    CHECK_S(c.v[0], "b");
    mt_strs_free(&c);
    mt_strs_free(&l);
    l = mt_words("   ");
    CHECK_U(l.n, 0);
    mt_strs_free(&l);
    l = mt_lines("a\r\nb\n\nc\n");
    CHECK_U(l.n, 4);
    CHECK_S(l.v[0], "a");
    CHECK_S(l.v[2], "");
    mt_strs_free(&l);
    l = mt_lines("");
    CHECK_U(l.n, 0);
    mt_strs_free(&l);
    char *a, *b;
    CHECK(mt_split_once("k = v = w", '=', &a, &b));
    CHECK_S(a, "k ");
    CHECK_S(b, " v = w");
    free(a);
    free(b);
    CHECK(!mt_split_once("kv", '=', &a, &b));
    char *t = mt_trim(" \t x y \n");
    CHECK_S(t, "x y");
    free(t);
    mt_buf buf = {0};
    CHECK_S(mt_str(&buf), "");
    mt_printf(&buf, "%d-%s", 7, "x");
    mt_putc(&buf, '!');
    t = mt_take(&buf);
    CHECK_S(t, "7-x!");
    free(t);
    t = mt_fmt("%05.1f", 2.25);
    CHECK_S(t, "002.2");
    char *s = NULL;
    mt_set(&s, t);
    free(t);
    CHECK(mt_eq(s, "002.2") && mt_starts(s, "00") && mt_ends(s, ".2") && mt_contains(s, "2."));
    free(s);
    return mesh_check_done();
}
