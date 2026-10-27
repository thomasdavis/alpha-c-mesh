/* programs.rs's reset_slot_leaves_the_gpu_run_request and table_seqlock. */
/* sexc compiles tests without the package's cflags (reported): the feature macro is here. */
#define _XOPEN_SOURCE 700
#include "mesh/check.h"
#include "mesh/manifest.h"
#include "mesh/manifest_encode.h"
#include "mesh/prog_table.h"

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

static mesh_mailbox mb;
static void *reader(void *arg) {
    static uint8_t e[MESH_PT_BYTES];
    mesh_pt_read_entry(mb, 3, e);
    atomic_store((_Atomic int *)arg, 1);
    return NULL;
}

int main(void) {
    mb = mesh_mailbox_test();
    for (size_t o = 0; o < MESH_PROG_CTL_BYTES; o += 4) mesh_mailbox_set(mb, mesh_prog_ctl(2) + o, 7);
    mesh_pt_reset_slot(mb, 2);
    int ok = 1;
    for (size_t o = 0; o < MESH_PROG_CTL_BYTES; o += 4) ok &= mesh_mailbox_get(mb, mesh_prog_ctl(2) + o) == (o == MESH_P_RUN_REQ ? 7u : 0u);
    CHECK(ok);
    CHECK_U(mesh_run_req_next(0, true), 3);
    CHECK_U(mesh_run_req_next(3, true), 5);
    CHECK_U(mesh_run_req_next(5, false), 6);
    CHECK_U(mesh_run_req_next(UINT32_MAX, false) >> 1, 0);

    mesh_manifest m;
    char *err = NULL;
    const char *text = "name = \"hello\"\nabi = 6\nview_bytes = 0x1000\ncounters = [\"tasks\"]";
    CHECK(mesh_manifest_parse(text, strlen(text), &m, &err));
    mesh_image im;
    memset(&im, 0, sizeof im);
    im.view_off = 0x100000;
    im.loads = 1;
    uint8_t entry[MESH_PT_BYTES], ext[MESH_PX_BYTES];
    mesh_manifest_encode_entry(&m, &im, entry);
    mesh_manifest_encode_ext(&m, ext);
    mesh_pt_write_entry(mb, 3, entry, ext, MESH_ST_LOADED);
    size_t e = mesh_prog_entry(3);
    CHECK(mesh_mailbox_get(mb, e + MESH_PT_GEN) == 2 && mesh_mailbox_get(mb, e + MESH_PT_STATE) == MESH_ST_LOADED &&
          mesh_mailbox_get(mb, MESH_H_PROG_SEQ) == 1);
    CHECK(mesh_mailbox_get(mb, e + MESH_PT_VIEW_OFF) == 0x100000 && mesh_mailbox_get(mb, e + MESH_PT_VIEW_BYTES) == 0x1000);
    uint8_t x[6];
    mesh_mailbox_read(mb, mesh_prog_ext(3), x, 6);
    CHECK(memcmp(x, "tasks\0", 6) == 0);
    uint32_t s;
    CHECK(mesh_pt_find(mb, "hello", &s) && s == 3);
    CHECK(!mesh_pt_find(mb, "hell", &s));
    uint8_t got[MESH_PT_BYTES];
    mesh_pt_read_entry(mb, 3, got);
    CHECK(memcmp(got + MESH_PT_NAME, "hello", 5) == 0);
    mesh_pt_set_state(mb, 3, MESH_ST_RUNNING);
    mesh_pt_set_state(mb, 3, MESH_ST_RUNNING);
    CHECK(mesh_mailbox_get(mb, e + MESH_PT_STATE) == MESH_ST_RUNNING && mesh_mailbox_get(mb, MESH_H_PROG_SEQ) == 2 &&
          mesh_mailbox_get(mb, e + MESH_PT_GEN) == 2);
    mesh_pt_clear(mb, 3);
    CHECK(mesh_mailbox_get(mb, e + MESH_PT_GEN) == 4 && mesh_mailbox_get(mb, e + MESH_PT_STATE) == MESH_ST_EMPTY &&
          mesh_mailbox_get(mb, e + MESH_PT_VIEW_OFF) == 0);
    CHECK(!mesh_pt_find(mb, "hello", &s));
    /* A writer mid-rewrite (odd gen) is waited out; even again: readable. */
    mesh_mailbox_set(mb, e + MESH_PT_GEN, 5);
    _Atomic int done = 0;
    pthread_t th;
    pthread_create(&th, NULL, reader, &done);
    struct timespec ts = {0, 5000000};
    nanosleep(&ts, NULL);
    CHECK(!atomic_load(&done));
    mesh_mailbox_publish(mb, e + MESH_PT_GEN, 6);
    pthread_join(th, NULL);
    CHECK(atomic_load(&done));

    /* Pipe blocks: written under the seqlock; the flight words left alone; unchanged: no gen bump. */
    uint8_t pp[MESH_PP_BYTES];
    memset(pp, 0, sizeof pp);
    pp[MESH_PP_IN_CAP] = 8;
    mesh_mailbox_set(mb, mesh_flight_word(3, 0), 9);
    mesh_pt_write_pipe(mb, 3, pp);
    CHECK(mesh_mailbox_get(mb, mesh_pipe_entry(3) + MESH_PP_IN_CAP) == 8 && mesh_mailbox_get(mb, mesh_flight_word(3, 0)) == 9);
    uint32_t g = mesh_mailbox_get(mb, e + MESH_PT_GEN);
    mesh_pt_write_pipe(mb, 3, pp);
    CHECK_U(mesh_mailbox_get(mb, e + MESH_PT_GEN), g);
    CHECK_U(mesh_pt_flight_epoch(mb), 0);
    CHECK_U(mesh_pt_flight_epoch(mb), 1);
    mesh_pt_flight_clear(mb, 3);
    CHECK_U(mesh_pt_flight(mb, 3, 0), 0);
    mesh_pt_set_hold(mb, 3, true);
    CHECK_U(mesh_mailbox_get(mb, mesh_pipe_entry(3) + MESH_PP_IN_HOLD), 1);
    mesh_manifest_free(&m);
    free(mb.base);
    return mesh_check_done();
}
