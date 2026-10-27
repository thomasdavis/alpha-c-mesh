/* place.rs frames_round_trip. */
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/abi/place_ops.h"
#include "mesh/check.h"
#include "mesh/place_wire.h"

#include <stdlib.h>

static mpw_place place_msg(const mpw_bytes *b) {
    CHECK(b->b[0] == MESH_F_PLACE);
    size_t n;
    char *err = NULL;
    CHECK(mpw_place_rest_len(b->b + 1, &n, &err));
    CHECK(b->n == 1 + MPW_PLACE_HEAD + n);
    mpw_place m;
    CHECK(mpw_place_decode(b->b + 1, b->b + 1 + MPW_PLACE_HEAD, n, &m, &err));
    return m;
}

static bool place_eq(const mpw_place *a, const mpw_place *b) {
    return a->op == b->op && a->id == b->id && !strcmp(a->name, b->name) && mpm_mode_eq(&a->mode, &b->mode) && a->aux == b->aux && a->nbytes == b->nbytes &&
           (!a->nbytes || !memcmp(a->bytes, b->bytes, a->nbytes));
}

int main(void) {
    uint8_t three[] = {1, 2, 3};
    mpw_place p = {.op = MESH_PL_SET, .id = 7, .name = "doom", .mode = mpm_split(mpm_assign_new((uint32_t[]){2, 0, 1, 1}, 4)), .aux = 77, .bytes = three, .nbytes = 3};
    mpw_bytes e = mpw_place_encode(&p);
    mpw_place d = place_msg(&e);
    CHECK(place_eq(&d, &p));
    mpw_place_free(&d);
    mpw_place q = {.op = MESH_PL_REQ, .id = 8, .name = "doom", .mode = mpm_pin(5), .aux = MPW_AUX_AUTO | 7};
    mpw_bytes eq = mpw_place_encode(&q);
    d = place_msg(&eq);
    CHECK(place_eq(&d, &q));
    mpw_place_free(&d);
    mpw_bytes_free(&eq);
    uint8_t st[12], data[64];
    memset(st, 9, sizeof st), memset(data, 5, sizeof data);
    mpw_frame f = {.name = "doom", .frame = 99, .parts = 3, .sys = 2, .state = st, .nstate = 12, .data = data, .ndata = 64};
    mpw_bytes fe = mpw_frame_encode(&f);
    CHECK(fe.b[0] == MESH_F_FRAME);
    size_t n;
    char *err = NULL;
    CHECK(mpw_frame_rest_len(fe.b + 1, &n, &err) && fe.n == 1 + MPW_FRAME_HEAD + n);
    mpw_frame g;
    CHECK(mpw_frame_decode(fe.b + 1, fe.b + 1 + MPW_FRAME_HEAD, n, &g, &err));
    CHECK(!strcmp(g.name, "doom") && g.frame == 99 && g.parts == 3 && g.sys == 2 && g.nstate == 12 && g.ndata == 64 && !memcmp(g.state, st, 12) && !memcmp(g.data, data, 64));
    mpw_frame_free(&g);
    mpw_bytes_free(&fe);
    mpw_bytes in = mpw_input_encode("doom", (const uint8_t *)"wasd", 4);
    CHECK(in.b[0] == MESH_F_INPUT && mpw_input_len(in.b + 1, &n, &err) && n == 4 && !memcmp(in.b + 1 + MPW_INPUT_HEAD, "wasd", 4));
    mpw_bytes_free(&in);
    /* Bounds: a hostile length is refused before anything is read. */
    mpw_bytes big = mpw_place_encode(&p);
    uint32_t over = MESH_F_PLACE_MAX + 1;
    memcpy(big.b + 1 + MPW_PLACE_HEAD - 4, &over, 4);
    CHECK(!mpw_place_rest_len(big.b + 1, &n, &err));
    free(err);
    mpw_bytes_free(&big);
    mpw_bytes bad = mpw_place_encode(&p);
    uint32_t op = 99;
    memcpy(bad.b + 1, &op, 4);
    CHECK(!mpw_place_decode(bad.b + 1, NULL, 0, &d, &err));
    CHECK_S(err, "F_PLACE: bad op 99");
    free(err);
    mpw_bytes_free(&bad);
    uint8_t huge[MESH_F_INPUT_MAX + 1] = {0};
    mpw_bytes hi = mpw_input_encode("doom", huge, sizeof huge);
    CHECK(!mpw_input_len(hi.b + 1, &n, &err));
    free(err);
    mpw_bytes_free(&hi);
    uint8_t nm[MESH_NAME_BYTES];
    mpw_name16("Doom", nm);
    char out[MESH_NAME_BYTES + 1];
    CHECK(!mpw_name_of(nm, MESH_NAME_BYTES, out, &err));
    CHECK_S(err, "bad program name \"Doom\"");
    free(err);
    mpw_name16("a-1", nm);
    CHECK(mpw_name_of(nm, MESH_NAME_BYTES, out, &err) && !strcmp(out, "a-1") && nm[3] == 0);
    mpw_bytes_free(&e);
    return mesh_check_done();
}
