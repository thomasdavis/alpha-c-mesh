/* views.rs's records_round_trip_and_json, one for one. */
#include "mesh/check.h"
#include "mesh/views.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    mesh_recs rs = MESH_RECS_INIT;
    mesh_rec *r = mesh_recs_add(&rs, "fleet");
    mesh_rec_kvu(r, "system", 0);
    mesh_rec_kvu(r, "leader", 1);
    mesh_rec_kvu(r, "term", 3);
    mesh_rec_none(r, "suspect");
    mesh_rec_kvu(r, "up", 0);
    mesh_rec_kv(r, "job", "1.2");
    r = mesh_recs_add(&rs, "member");
    mesh_rec_kvu(r, "id", 2);
    mesh_rec_kv(r, "host", "mac studio");
    mesh_rec_kv(r, "share", "0.48");
    mesh_rec_kv(r, "eq", "a=b");
    mesh_rec_text(r, "busy: cpu \"psi\"\n0.31");
    r = mesh_recs_add(&rs, "member");
    mesh_rec_kvu(r, "id", 3);
    mesh_rec_kv(r, "host", "");
    mesh_rec_f(r, "share", true, 0.5, 2);
    mesh_rec_f(r, "cpu", true, NAN, 2);
    mesh_rec_kv(r, "zip", "007");

    mesh_buf t = MESH_BUF_INIT;
    mesh_views_text(&t, &rs);
    CHECK_S(mesh_buf_str(&t), "fleet system=0 leader=1 term=3 suspect=- up=0 job=1.2\nmember id=2 host=mac_studio share=0.48 eq=a_b -- busy: cpu \"psi\" 0.31\nmember id=3 host=- share=0.50 cpu=- zip=007\n");

    /* Parsed back: the same records. */
    mesh_recs back = MESH_RECS_INIT;
    mesh_views_parse(mesh_buf_str(&t), &back);
    CHECK_U(back.len, 3);
    mesh_buf t2 = MESH_BUF_INIT;
    mesh_views_text(&t2, &back);
    CHECK_S(mesh_buf_str(&t2), mesh_buf_str(&t));
    CHECK_S(mesh_rec_get(&rs.items[1], "host"), "mac_studio");
    CHECK(mesh_rec_get(&rs.items[2], "host") == NULL);
    CHECK_S(back.items[1].text, "busy: cpu \"psi\" 0.31");

    mesh_buf j = MESH_BUF_INIT;
    mesh_views_json(&j, &rs);
    CHECK_S(mesh_buf_str(&j), "{\"fleet\":[{\"system\":0,\"leader\":1,\"term\":3,\"suspect\":null,\"up\":\"0\",\"job\":\"1.2\"}],\"member\":[{\"id\":2,\"host\":\"mac_studio\",\"share\":0.48,\"eq\":\"a_b\",\"text\":\"busy: cpu \\\"psi\\\" 0.31\"},{\"id\":3,\"host\":null,\"share\":0.50,\"cpu\":null,\"zip\":\"007\"}]}\n");

    struct { const char *v; bool n; } nums[] = {{"0", true}, {"-1.5e3", true}, {"1.", false}, {"1,2", false}, {"0x10", false},
                                               {"-", false}, {"12ms", false}, {"00", false}};
    for (size_t i = 0; i < sizeof nums / sizeof nums[0]; i++) CHECK(mesh_views_json_number(nums[i].v) == nums[i].n);

    bool json = true;
    char *err = NULL;
    CHECK_I(mesh_views_format_of("", &json, &err), 0);
    CHECK(!json);
    CHECK_I(mesh_views_format_of("a=1&format=json", &json, &err), 0);
    CHECK(json);
    CHECK_I(mesh_views_format_of("format=xml", &json, &err), -1);
    CHECK_S(err, "format=xml: json or text");
    free(err);

    /* A unicode space is one token separator too, and \r\n lines parse. */
    mesh_recs u = MESH_RECS_INIT;
    r = mesh_recs_add(&u, "x");
    mesh_rec_kv(r, "h", "a\xc2\xa0" "b");
    CHECK_S(u.items[0].kv[0].v, "a_b");
    mesh_views_parse("k a=1\r\n\r\n  \nk b=2 c\n", &back);
    CHECK_U(back.len, 5);
    CHECK_S(back.items[3].kv[0].v, "1");
    CHECK_S(back.items[4].kv[1].k, "c");
    CHECK_S(back.items[4].kv[1].v, "");

    mesh_recs cp = MESH_RECS_INIT;
    mesh_recs_extend(&cp, &rs);
    mesh_buf t3 = MESH_BUF_INIT;
    mesh_views_render(&t3, &cp, false);
    CHECK_S(mesh_buf_str(&t3), mesh_buf_str(&t));
    mesh_recs_free(&rs);
    mesh_recs_free(&back);
    mesh_recs_free(&u);
    mesh_recs_free(&cp);
    mesh_buf_free(&t);
    mesh_buf_free(&t2);
    mesh_buf_free(&t3);
    mesh_buf_free(&j);
    return mesh_check_done();
}
