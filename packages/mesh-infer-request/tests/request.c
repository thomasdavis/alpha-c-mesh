/* infer.rs's requests_parse, plus the error texts. */
#include "mesh/check.h"
#include "mesh/infer_request.h"

#include <stdlib.h>

int main(void) {
    mesh_infer_request r;
    char *err = NULL;
    CHECK_I(mesh_infer_parse("exact max 16 prefill=0 decode=3 The quick  brown fox", &r, &err), 0);
    CHECK_S(r.mode, "exact");
    CHECK_U(r.max, 16);
    CHECK(r.has_prefill && r.prefill == 0 && r.has_decode && r.decode == 3 && !r.has_mono);
    CHECK_S(r.prompt, "The quick  brown fox");
    mesh_infer_request_free(&r);
    CHECK_I(mesh_infer_parse("Hello there", &r, &err), 0);
    CHECK_S(r.mode, "fast");
    CHECK_U(r.max, 32);
    CHECK_S(r.prompt, "Hello there");
    mesh_infer_request_free(&r);
    CHECK_I(mesh_infer_parse("max=5 mono=1 hi", &r, &err), 0);
    CHECK(r.has_mono && r.mono == 1 && r.max == 5);
    mesh_infer_request_free(&r);
    CHECK_I(mesh_infer_parse("prefill=1 hi", &r, &err), -1);
    CHECK_S(err, "prefill= and decode= go together (" MESH_INFER_USAGE ")");
    free(err);
    mesh_infer_request_free(&r);
    CHECK_I(mesh_infer_parse("mono=1 prefill=0 decode=1 hi", &r, &err), -1);
    free(err);
    mesh_infer_request_free(&r);
    CHECK_I(mesh_infer_parse("exact max 8", &r, &err), -1);
    CHECK_S(err, "no prompt (" MESH_INFER_USAGE ")");
    free(err);
    mesh_infer_request_free(&r);
    CHECK_I(mesh_infer_parse("max x hi", &r, &err), -1);
    CHECK_S(err, "max: not a number (" MESH_INFER_USAGE ")");
    free(err);
    mesh_infer_request_free(&r);
    CHECK_I(mesh_infer_parse("max=0 hi", &r, &err), -1);
    CHECK_S(err, "max 0: 1..=1000");
    free(err);
    mesh_infer_request_free(&r);
    CHECK_I(mesh_infer_parse("repeat=5000 len=7 hi  ", &r, &err), 0);
    CHECK(r.repeat == 1000 && r.len == 7);
    CHECK_S(r.prompt, "hi");
    mesh_infer_request_free(&r);

    bool full;
    CHECK_I(mesh_infer_from_query("prompt=Hello%2C+world&max=4&mode=exact&prefill=0&decode=1&format=full", &r, &full, &err), 0);
    CHECK_S(r.prompt, "Hello, world");
    CHECK(r.max == 4 && r.prefill == 0 && r.decode == 1 && full);
    CHECK_S(r.mode, "exact");
    mesh_infer_request_free(&r);
    CHECK_I(mesh_infer_from_query("max=4", &r, &full, &err), -1);
    CHECK_S(err, "no prompt= parameter");
    free(err);
    CHECK_I(mesh_infer_from_query("prompt=x&evil=1", &r, &full, &err), -1);
    CHECK_S(err, "unknown parameter \"evil\" (prompt, max, mode, prefill, decode, mono, format)");
    free(err);
    char *d = mesh_infer_url_decode("a%zz%41%4");
    CHECK_S(d, "a%zzA%4");
    free(d);

    mesh_infer_msg m;
    mesh_infer_msg_init(&m);
    mesh_infer_msg_set(&m, "tok_s", "41.2");
    mesh_infer_msg_set(&m, "gen_tokens", "8");
    mesh_infer_msg_set_body(&m, "the end");
    mesh_buf t = MESH_BUF_INIT;
    mesh_infer_text(&t, &m);
    CHECK_S(mesh_buf_str(&t), "gen_tokens 8\ntok_s 41.2\ntext the end\n");
    mesh_buf_free(&t);
    mesh_infer_msg_free(&m);
    return mesh_check_done();
}
