/* Answered, dropped unanswered, answered too late, and across threads. */
#include "mesh/check.h"
#include "mesh/buf.h"
#include "mesh/reply.h"

#include <stdlib.h>

#include <pthread.h>

static void *answer_later(void *arg) {
    mesh_sleep_us(20000);
    mesh_reply_okf(arg, "%s %d", "blink", 412);
    return NULL;
}

int main(void) {
    mesh_reply *r = mesh_reply_new();
    mesh_reply_ok(mesh_reply_sender(r), "slot 3");
    mesh_result x;
    CHECK_I(mesh_reply_wait(r, 100, &x), MESH_CHAN_OK);
    CHECK(x.ok);
    CHECK_S(x.text, "slot 3");
    mesh_result_free(&x);
    mesh_reply_release(r);

    /* Dropped unanswered: the waiter knows at once. */
    r = mesh_reply_new();
    mesh_reply_drop(mesh_reply_sender(r));
    int64_t t0 = mesh_mono_ms();
    CHECK_I(mesh_reply_wait(r, 5000, &x), MESH_CHAN_CLOSED);
    CHECK(mesh_mono_ms() - t0 < 1000);
    mesh_reply_release(r);

    /* A thread answers within the timeout. */
    r = mesh_reply_new();
    pthread_t th;
    pthread_create(&th, NULL, answer_later, mesh_reply_sender(r));
    CHECK_I(mesh_reply_wait(r, 5000, &x), MESH_CHAN_OK);
    CHECK_S(x.text, "blink 412");
    mesh_result_free(&x);
    pthread_join(th, NULL);
    mesh_reply_release(r);

    /* Too late: the waiter gave up; the answer is freed with the channel (no leak under ASan). */
    r = mesh_reply_new();
    mesh_reply *s = mesh_reply_sender(r);
    CHECK_I(mesh_reply_wait(r, 10, &x), MESH_CHAN_TIMEOUT);
    mesh_reply_release(r);
    mesh_reply_errf(s, "no program %s", "\"x\"");

    /* Values and bytes; clones. */
    r = mesh_reply_new();
    mesh_reply *c = mesh_reply_clone(r);
    mesh_reply_value(mesh_reply_sender(r), 7);
    mesh_reply_bytes(c, "abc", 3);
    CHECK_I(mesh_reply_wait(r, 100, &x), MESH_CHAN_OK);
    CHECK_U(x.value, 7);
    CHECK_I(mesh_reply_wait(r, 100, &x), MESH_CHAN_OK);
    CHECK_U(x.len, 3);
    CHECK(x.bytes[0] == 'a' && x.bytes[2] == 'c');
    mesh_result_free(&x);
    mesh_reply_release(r);

    /* A pointer payload: taken, or freed with the channel when nobody waits. */
    r = mesh_reply_new();
    mesh_reply_ptr(mesh_reply_sender(r), mesh_xstrdup("inventory"), free);
    CHECK_I(mesh_reply_wait(r, 100, &x), MESH_CHAN_OK);
    CHECK_S(x.ptr, "inventory");
    mesh_result_free(&x);
    mesh_reply_release(r);
    r = mesh_reply_new();
    s = mesh_reply_sender(r);
    mesh_reply_release(r);
    mesh_reply_ptr(s, mesh_xstrdup("nobody"), free);
    return mesh_check_done();
}
