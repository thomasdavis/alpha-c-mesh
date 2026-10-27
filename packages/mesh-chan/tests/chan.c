/* Order, growth, bounds, timeouts, disconnects and drops, with threads. */
#include "mesh/chan.h"
#include "mesh/check.h"

#include <pthread.h>

static int dropped;
static void count_drop(void *item) { dropped += *(int *)item; }

static void *producer(void *arg) {
    mesh_chan *c = arg;
    for (int i = 0; i < 10000; i++) mesh_chan_send(c, &i);
    mesh_chan_drop_sender(c);
    mesh_chan_release(c);
    return NULL;
}

int main(void) {
    mesh_chan *c = mesh_chan_new(sizeof(int), 0, NULL);
    for (int i = 0; i < 100; i++) CHECK_I(mesh_chan_send(c, &i), MESH_CHAN_OK);
    CHECK_U(mesh_chan_len(c), 100);
    int v = -1, ok = 1;
    for (int i = 0; i < 100; i++) ok &= mesh_chan_try_recv(c, &v) == MESH_CHAN_OK && v == i;
    CHECK(ok);
    CHECK_I(mesh_chan_try_recv(c, &v), MESH_CHAN_EMPTY);
    int64_t t0 = mesh_mono_ms();
    CHECK_I(mesh_chan_recv(c, &v, 50), MESH_CHAN_TIMEOUT);
    int64_t dt = mesh_mono_ms() - t0;
    CHECK(dt >= 45 && dt < 1000);
    /* A producer thread: every message, in order, then disconnected. */
    mesh_chan_retain(c);
    mesh_chan_add_sender(c); /* the thread's */
    pthread_t th;
    pthread_create(&th, NULL, producer, c);
    mesh_chan_drop_sender(c); /* ours: only the thread's remains */
    long sum = 0;
    int n = 0, last = -1, ordered = 1;
    while (mesh_chan_recv(c, &v, -1) == MESH_CHAN_OK) { ordered &= v == last + 1; last = v; sum += v; n++; }
    pthread_join(th, NULL);
    CHECK(ordered);
    CHECK_I(n, 10000);
    CHECK_I(sum, 49995000L);
    CHECK_I(mesh_chan_try_recv(c, &v), MESH_CHAN_CLOSED);
    mesh_chan_release(c);

    /* Bounded: full; the receiver gone: closed, and queued messages dropped. */
    mesh_chan *b = mesh_chan_new(sizeof(int), 2, count_drop);
    int one = 1, two = 2, three = 3;
    CHECK_I(mesh_chan_try_send(b, &one), MESH_CHAN_OK);
    CHECK_I(mesh_chan_try_send(b, &two), MESH_CHAN_OK);
    CHECK_I(mesh_chan_try_send(b, &three), MESH_CHAN_FULL);
    mesh_chan_close_rx(b);
    CHECK_I(dropped, 3);
    CHECK_I(mesh_chan_try_send(b, &three), MESH_CHAN_CLOSED);
    mesh_chan_release(b);

    /* Released with messages queued: they are dropped. */
    dropped = 0;
    mesh_chan *d = mesh_chan_new(sizeof(int), 0, count_drop);
    mesh_chan_send(d, &two);
    mesh_chan_send(d, &three);
    mesh_chan_release(d);
    CHECK_I(dropped, 5);
    return mesh_check_done();
}
