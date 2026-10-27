/* infer.rs's messages_and_layers_round_trip, over a socketpair. */
#include "mesh/check.h"
#include "mesh/infer_wire.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
    mesh_infer_msg m;
    mesh_infer_msg_init(&m);
    mesh_infer_msg_set(&m, "mode", "exact");
    mesh_infer_msg_setf(&m, "max", "%d", 7);
    mesh_infer_msg_set(&m, "x", "a\nb");
    mesh_buf e = MESH_BUF_INIT;
    mesh_infer_msg_encode(&m, &e);
    CHECK_S(mesh_buf_str(&e), "max=7\nmode=exact\nx=a b\n\n");
    mesh_infer_msg_set_body(&m, "line one\n\nline three");
    mesh_buf_clear(&e);
    mesh_infer_msg_encode(&m, &e);
    mesh_infer_msg d;
    char *err = NULL;
    CHECK_I(mesh_infer_msg_decode(e.data, e.len, &d, &err), 0);
    CHECK_S(mesh_infer_msg_get(&d, "mode"), "exact");
    CHECK_S(mesh_infer_msg_get(&d, "x"), "a b");
    CHECK_S(d.body, "line one\n\nline three");
    uint64_t v;
    CHECK_I(mesh_infer_msg_num(&d, "max", &v, &err), 0);
    CHECK_U(v, 7);
    CHECK_I(mesh_infer_msg_num(&d, "mode", &v, &err), -1);
    CHECK_S(err, "mode: not a number");
    free(err);
    CHECK_I(mesh_infer_msg_num(&d, "nope", &v, &err), -1);
    CHECK_S(err, "missing nope");
    free(err);
    mesh_infer_msg_free(&d);
    /* No body: a message that ends in its blank line. */
    mesh_infer_msg_set_body(&m, "");
    mesh_buf_clear(&e);
    mesh_infer_msg_encode(&m, &e);
    CHECK_I(mesh_infer_msg_decode(e.data, e.len, &d, &err), 0);
    CHECK_S(d.body, "");
    mesh_infer_msg_free(&d);
    CHECK_I(mesh_infer_msg_decode("a=1\nbad\n\nx", 10, &d, &err), -1);
    CHECK_S(err, "a header line without '=': \"bad\"");
    free(err);
    CHECK_I(mesh_infer_msg_decode("\xff", 1, &d, &err), -1);
    CHECK_S(err, "a message that is not UTF-8");
    free(err);

    unsigned char bytes[1001];
    for (unsigned i = 0; i < 1001; i++) bytes[i] = (unsigned char)(i * 7);
    mesh_buf f = MESH_BUF_INIT;
    mesh_infer_layer_frame(&f, 5, 3, bytes, sizeof bytes);
    uint32_t l, rows;
    const uint8_t *p;
    size_t n;
    CHECK_I(mesh_infer_parse_layer(f.data, f.len, &l, &rows, &p, &n, &err), 0);
    CHECK(l == 5 && rows == 3 && n == 1001 && memcmp(p, bytes, n) == 0);
    f.data[100] ^= 1;
    CHECK_I(mesh_infer_parse_layer(f.data, f.len, &l, &rows, &p, &n, &err), -1);
    CHECK_S(err, "KV layer 5: checksum mismatch (1001 bytes)");
    free(err);
    f.data[100] ^= 1;

    int sv[2];
    CHECK_I(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
    CHECK_I(mesh_infer_write_frame(sv[0], MESH_INFER_K_LAYER, f.data, f.len), 0);
    CHECK_I(mesh_infer_write_frame(sv[0], MESH_INFER_K_FIRST, "token=3\n", 8), 0);
    close(sv[0]);
    uint8_t kind;
    mesh_buf body = MESH_BUF_INIT;
    CHECK_I(mesh_infer_read_frame(sv[1], &kind, &body, NULL, &err), 0);
    CHECK(kind == MESH_INFER_K_LAYER && body.len == f.len && memcmp(body.data, f.data, f.len) == 0);
    mesh_buf_clear(&body);
    CHECK_I(mesh_infer_read_frame(sv[1], &kind, &body, NULL, &err), 0);
    CHECK(kind == MESH_INFER_K_FIRST);
    CHECK_S(mesh_buf_str(&body), "token=3\n");
    CHECK_I(mesh_infer_read_frame(sv[1], &kind, &body, NULL, &err), -1);
    CHECK_S(err, "read: failed to fill whole buffer");
    free(err);
    close(sv[1]);
    mesh_infer_msg_free(&m);
    mesh_buf_free(&e);
    mesh_buf_free(&f);
    mesh_buf_free(&body);
    return mesh_check_done();
}
