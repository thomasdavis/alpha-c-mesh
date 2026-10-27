/* peer.rs view_chunks_are_bounded_and_counted_once; LoadAvg's smoothing. */
#include "mesh/check.h"
#include "mesh/peer_view.h"
#include "mesh/text.h"

#include <stdlib.h>

int main(void) {
    uint8_t ones[16], sevens[0x1000], nines[0x1000];
    memset(ones, 1, sizeof ones), memset(sevens, 7, sizeof sevens), memset(nines, 9, sizeof nines);
    char *err = NULL;
    mpv_wait a = mpv_wait_new(1, 0, 0x2000);
    CHECK(mpv_take(&a, UINT32_MAX, 0, ones, 1, &err) == -1 && mt_contains(err, "at most 8192"));
    free(err);
    CHECK(a.buf == NULL);
    mpv_wait_free(&a);
    mpv_wait b = mpv_wait_new(1, 0, 0x2000);
    CHECK(mpv_take(&b, 0x2000, 0, sevens, 0x1000, &err) == 0);
    CHECK(mpv_take(&b, 0x2000, 0, sevens, 0x1000, &err) == 0);
    CHECK(mpv_take(&b, 0x2000, 0x1000, nines, 0x1000, &err) == 1);
    CHECK(b.buf[0] == 7 && b.buf[0xfff] == 7 && b.buf[0x1000] == 9 && b.buf[0x1fff] == 9);
    mpv_wait_free(&b);
    mpv_wait c = mpv_wait_new(1, 0, 0x2000);
    CHECK(mpv_take(&c, 0x2000, 0, ones, 16, &err) == 0);
    CHECK(mpv_take(&c, 0x1000, 16, ones, 16, &err) == -1 && mt_contains(err, "disagree"));
    free(err);
    CHECK(mpv_take(&c, 0x2000, 0x1ff8, ones, 16, &err) == -1 && mt_contains(err, "past"));
    free(err);
    mpv_wait_free(&c);
    mpv_wait d = mpv_wait_new(1, 0, 0x2000);
    CHECK(mpv_take(&d, 0, 0, NULL, 0, &err) == 1);
    mpv_wait_free(&d);
    mpv_load l = {.at = 0, .cpu = 0.5f, .psi_cpu = mof32_some(0.1f), .share = mof32_none()};
    mpv_load_add(&l, 1, 1.0f, mof32_some(0.6f), mof32_some(0.2f), mof32_some(0.4f));
    CHECK(l.cpu == 0.5f + 0.2f * (1.0f - 0.5f) && l.psi_cpu.v == 0.1f + 0.2f * (0.6f - 0.1f) && l.psi_mem.v == 0.2f && l.share.v == 0.4f);
    float before = l.cpu;
    mpv_load_add(&l, 1, 0.0f, mof32_none(), mof32_none(), mof32_none());
    CHECK(l.cpu == before && l.psi_cpu.some);
    mpv_load_add(&l, 2, 0.0f, mof32_none(), mof32_none(), mof32_none());
    CHECK(!l.psi_cpu.some && !l.share.some);
    return mesh_check_done();
}
