/* mps.rs's frames test, through a pipe, plus the size refusals. */
#include "mesh/check.h"
#include "mesh/mps_wire.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    const uint8_t body[] = {1, 0, 0, 0, 2, 0, 0, 0};
    uint8_t *f;
    size_t n;
    mesh_mps_frame(MESH_W_LAUNCH, body, 8, &f, &n);
    CHECK(n == 13 && f[0] == 9 && f[1] == 0 && f[2] == 0 && f[3] == 0 && f[4] == MESH_W_LAUNCH);
    int p[2];
    CHECK(pipe(p) == 0);
    CHECK(write(p[1], f, n) == (ssize_t)n);
    free(f);
    uint8_t op, *b;
    size_t bn;
    CHECK(mesh_mps_read_frame(p[0], &op, &b, &bn));
    CHECK(op == MESH_W_LAUNCH && bn == 8);
    CHECK_U(mesh_mps_u32_at(b, bn, 0), 1);
    CHECK_U(mesh_mps_u32_at(b, bn, 4), 2);
    CHECK_U(mesh_mps_u32_at(b, bn, 8), 0);
    free(b);
    /* A zero-length frame ends the stream. */
    const uint8_t zero[4] = {0, 0, 0, 0};
    CHECK(write(p[1], zero, 4) == 4);
    CHECK(!mesh_mps_read_frame(p[0], &op, &b, &bn));
    /* write_frame and EOF. */
    uint8_t *body2 = NULL;
    size_t b2 = 0;
    mesh_mps_put_u32(&body2, &b2, 7);
    mesh_mps_put_u64(&body2, &b2, UINT64_C(0x1122334455667788));
    mesh_mps_put_bytes(&body2, &b2, "xy", 2);
    CHECK(mesh_mps_write_frame(p[1], MESH_W_DONE, body2, b2));
    free(body2);
    close(p[1]);
    CHECK(mesh_mps_read_frame(p[0], &op, &b, &bn));
    CHECK(op == MESH_W_DONE && bn == 14);
    CHECK_U(mesh_mps_u32_at(b, bn, 0), 7);
    CHECK_U(mesh_mps_u64_at(b, bn, 4), UINT64_C(0x1122334455667788));
    CHECK(memcmp(b + 12, "xy", 2) == 0);
    free(b);
    CHECK(!mesh_mps_read_frame(p[0], &op, &b, &bn));
    close(p[0]);
    return mesh_check_done();
}
