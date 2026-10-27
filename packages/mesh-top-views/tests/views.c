/* crates/mesh-top/tests/views.rs's records_parse, ported, on its FLEET fixture. */
#include "mesh/check.h"
#include "mesh/top_views.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FILE *f = fopen("tests/data/fleet.txt", "rb");
    CHECK(f != NULL);
    if (!f) return mesh_check_done();
    char *t = calloc(1, 16384);
    CHECK(fread(t, 1, 16383, f) > 0);
    fclose(f);
    mt_vrec *r;
    size_t n = mt_views_parse(t, &r);
    CHECK_S(r[0].kind, "fleet");
    uint64_t u;
    CHECK(mt_vrec_u(&r[0], "leader", &u) && u == 1);
    CHECK_S(mt_vrec_get(&r[0], "suspect"), "3");
    uint32_t list[8];
    CHECK_U(mt_vrec_list(&r[0], "cordon", list, 8), 1);
    CHECK_U(list[0], 2);
    const mt_vrec *m3 = NULL;
    size_t cells = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(r[i].kind, "member") == 0 && mt_vrec_u(&r[i], "id", &u) && u == 3) m3 = &r[i];
        cells += strcmp(r[i].kind, "cell") == 0;
    }
    CHECK(m3 && mt_vrec_flag(m3, "busy") && mt_vrec_get(m3, "share") == NULL);
    CHECK_S(m3->text, "busy: cpu 0.91 for 6 s");
    CHECK_U(cells, 20);
    mt_vrecs_free(r, n);
    mt_view v = mt_view_new(0, 0, "err view \"x\": fleet, jobs or pipes [json]\n");
    CHECK(v.n == 0 && v.err);
    CHECK_S(v.err, "view \"x\": fleet, jobs or pipes [json]");
    mt_view_free(&v);
    double d;
    CHECK(mt_parse_f64("0.48", &d) && d == 0.48);
    CHECK(mt_parse_f64("-", &d) == false);
    CHECK(mt_parse_f64("1e3", &d) && d == 1000.0);
    CHECK(!mt_parse_f64("0x10", &d));
    CHECK(mt_parse_u64("+7", &u) && u == 7);
    CHECK(!mt_parse_u64("-7", &u));
    free(t);
    return mesh_check_done();
}
