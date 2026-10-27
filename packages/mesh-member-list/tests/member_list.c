/* fleet.rs list / parse_list / mask_of / members_of and sched::at_word. */
#include "mesh/check.h"
#include "mesh/member_list.h"

#include <stdlib.h>

int main(void) {
    ml_list l;
    char *err = NULL;
    CHECK(ml_parse("0,2,3", &l, &err));
    CHECK_U(ml_mask(&l), 0xd);
    char *t = ml_text(&l);
    CHECK_S(t, "0,2,3");
    free(t);
    ml_list m = ml_members(0xd);
    CHECK(ml_eq(&l, &m));
    ml_free(&m);
    ml_free(&l);
    CHECK(ml_parse("-", &l, &err) && l.n == 0);
    t = ml_text(&l);
    CHECK_S(t, "-");
    free(t);
    CHECK(!ml_parse("1,x", &l, &err));
    CHECK_S(err, "bad member list \"1,x\"");
    free(err);
    CHECK(!ml_parse("8", &l, &err));
    CHECK_S(err, "member 8 >= 8");
    free(err);
    CHECK(!ml_parse("", &l, &err));
    CHECK_S(err, "bad member list \"\"");
    free(err);
    ml_opt o = ml_none();
    t = ml_at_word(&o);
    CHECK_S(t, "all");
    free(t);
    CHECK(ml_opt_has_or_all(&o, 5));
    o = ml_some((uint32_t[]){3, 1}, 2);
    CHECK(!ml_opt_has_or_all(&o, 0) && ml_opt_has_or_all(&o, 1));
    ml_sort(&o.l);
    t = ml_at_word(&o);
    CHECK_S(t, "1,3");
    free(t);
    ml_opt c = ml_opt_clone(&o);
    CHECK(ml_opt_eq(&c, &o));
    ml_remove_value(&c.l, 1);
    CHECK(!ml_opt_eq(&c, &o) && c.l.n == 1);
    ml_opt_free(&c);
    ml_opt_free(&o);
    CHECK_I(ml_popcount(0xf0f0), 8);
    return mesh_check_done();
}
