/* A ring of fixed-size messages under a mutex, with two condition variables on CLOCK_MONOTONIC. */
#include "mesh/chan.h"
#include "mesh/buf.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct mesh_chan {
    pthread_mutex_t mu;
    pthread_cond_t can_recv, can_send;
    size_t item, bound;
    unsigned char *ring;
    size_t cap, head, len;
    int refs, senders, rx_open;
    void (*drop)(void *item);
};

int64_t mesh_mono_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

int64_t mesh_mono_ms(void) { return mesh_mono_us() / 1000; }

void mesh_sleep_us(int64_t us) {
    if (us <= 0) return;
    struct timespec t = {(time_t)(us / 1000000), (long)(us % 1000000) * 1000};
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {}
}

mesh_chan *mesh_chan_new(size_t item, size_t bound, void (*drop)(void *item)) {
    mesh_chan *c = mesh_xcalloc(1, sizeof *c);
    pthread_mutex_init(&c->mu, NULL);
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(&c->can_recv, &a);
    pthread_cond_init(&c->can_send, &a);
    pthread_condattr_destroy(&a);
    c->item = item ? item : 1;
    c->bound = bound;
    c->cap = bound ? bound : 16;
    c->ring = mesh_xmalloc(c->cap * c->item);
    c->refs = 1;
    c->senders = 1;
    c->rx_open = 1;
    c->drop = drop;
    return c;
}

static void drain_locked(mesh_chan *c) {
    while (c->len) {
        if (c->drop) c->drop(c->ring + c->head * c->item);
        c->head = (c->head + 1) % c->cap;
        c->len--;
    }
}

void mesh_chan_retain(mesh_chan *c) {
    pthread_mutex_lock(&c->mu);
    c->refs++;
    pthread_mutex_unlock(&c->mu);
}

void mesh_chan_release(mesh_chan *c) {
    if (!c) return;
    pthread_mutex_lock(&c->mu);
    int last = --c->refs == 0;
    if (last) drain_locked(c);
    pthread_mutex_unlock(&c->mu);
    if (!last) return;
    pthread_mutex_destroy(&c->mu);
    pthread_cond_destroy(&c->can_recv);
    pthread_cond_destroy(&c->can_send);
    free(c->ring);
    free(c);
}

void mesh_chan_add_sender(mesh_chan *c) {
    pthread_mutex_lock(&c->mu);
    c->senders++;
    pthread_mutex_unlock(&c->mu);
}

void mesh_chan_drop_sender(mesh_chan *c) {
    pthread_mutex_lock(&c->mu);
    if (c->senders > 0 && --c->senders == 0) pthread_cond_broadcast(&c->can_recv);
    pthread_mutex_unlock(&c->mu);
}

void mesh_chan_close_rx(mesh_chan *c) {
    pthread_mutex_lock(&c->mu);
    c->rx_open = 0;
    drain_locked(c);
    pthread_cond_broadcast(&c->can_send);
    pthread_mutex_unlock(&c->mu);
}

static void push_locked(mesh_chan *c, const void *item) {
    if (c->len == c->cap) {
        /* Unbounded: grow, keeping order. */
        size_t cap = c->cap * 2;
        unsigned char *r = mesh_xmalloc(cap * c->item);
        for (size_t i = 0; i < c->len; i++) memcpy(r + i * c->item, c->ring + ((c->head + i) % c->cap) * c->item, c->item);
        free(c->ring);
        c->ring = r;
        c->cap = cap;
        c->head = 0;
    }
    memcpy(c->ring + ((c->head + c->len) % c->cap) * c->item, item, c->item);
    c->len++;
    pthread_cond_signal(&c->can_recv);
}

static int send_(mesh_chan *c, const void *item, int wait) {
    pthread_mutex_lock(&c->mu);
    for (;;) {
        if (!c->rx_open) { pthread_mutex_unlock(&c->mu); return MESH_CHAN_CLOSED; }
        if (!c->bound || c->len < c->bound) break;
        if (!wait) { pthread_mutex_unlock(&c->mu); return MESH_CHAN_FULL; }
        pthread_cond_wait(&c->can_send, &c->mu);
    }
    push_locked(c, item);
    pthread_mutex_unlock(&c->mu);
    return MESH_CHAN_OK;
}

int mesh_chan_send(mesh_chan *c, const void *item) { return send_(c, item, 1); }
int mesh_chan_try_send(mesh_chan *c, const void *item) { return send_(c, item, 0); }

static void deadline(struct timespec *t, int64_t ms) {
    clock_gettime(CLOCK_MONOTONIC, t);
    t->tv_sec += (time_t)(ms / 1000);
    t->tv_nsec += (long)(ms % 1000) * 1000000;
    if (t->tv_nsec >= 1000000000) { t->tv_sec++; t->tv_nsec -= 1000000000; }
}

int mesh_chan_recv(mesh_chan *c, void *out, int64_t timeout_ms) {
    struct timespec until;
    if (timeout_ms >= 0) deadline(&until, timeout_ms);
    pthread_mutex_lock(&c->mu);
    while (!c->len) {
        if (!c->senders) { pthread_mutex_unlock(&c->mu); return MESH_CHAN_CLOSED; }
        if (timeout_ms == 0) { pthread_mutex_unlock(&c->mu); return MESH_CHAN_TIMEOUT; }
        if (timeout_ms < 0) pthread_cond_wait(&c->can_recv, &c->mu);
        else if (pthread_cond_timedwait(&c->can_recv, &c->mu, &until) == ETIMEDOUT && !c->len) {
            int closed = !c->senders;
            pthread_mutex_unlock(&c->mu);
            return closed ? MESH_CHAN_CLOSED : MESH_CHAN_TIMEOUT;
        }
    }
    memcpy(out, c->ring + c->head * c->item, c->item);
    c->head = (c->head + 1) % c->cap;
    c->len--;
    pthread_cond_signal(&c->can_send);
    pthread_mutex_unlock(&c->mu);
    return MESH_CHAN_OK;
}

int mesh_chan_try_recv(mesh_chan *c, void *out) {
    pthread_mutex_lock(&c->mu);
    if (!c->len) {
        int closed = !c->senders;
        pthread_mutex_unlock(&c->mu);
        return closed ? MESH_CHAN_CLOSED : MESH_CHAN_EMPTY;
    }
    memcpy(out, c->ring + c->head * c->item, c->item);
    c->head = (c->head + 1) % c->cap;
    c->len--;
    pthread_cond_signal(&c->can_send);
    pthread_mutex_unlock(&c->mu);
    return MESH_CHAN_OK;
}

size_t mesh_chan_len(mesh_chan *c) {
    pthread_mutex_lock(&c->mu);
    size_t n = c->len;
    pthread_mutex_unlock(&c->mu);
    return n;
}
