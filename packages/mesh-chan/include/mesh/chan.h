/* @ajax/mesh-chan: a channel of fixed-size messages between threads, with the
 * semantics meshd's Rust relied on (std::sync::mpsc):
 *
 * - any number of senders, one receiver; messages arrive in send order;
 * - unbounded (bound 0), or bounded: try_send refuses when full (sync_channel);
 * - a send fails once the receiver is gone (close_rx); a receive reports
 *   "disconnected" once every sender is gone and nothing is left;
 * - receives wait forever, for a timeout (monotonic clock), or not at all.
 *
 * Lifetime is a reference count (retain/release): a channel lives until its
 * last holder lets go, so a reply sent to a requester that already gave up is
 * just dropped. Messages still queued then are passed to `drop` (when given),
 * so what they own is freed. */
#ifndef MESH_CHAN_H
#define MESH_CHAN_H

#include <stddef.h>
#include <stdint.h>

typedef struct mesh_chan mesh_chan;

enum {
    MESH_CHAN_OK = 0,
    MESH_CHAN_EMPTY = 1,   /* try_recv: nothing queued */
    MESH_CHAN_TIMEOUT = 2, /* recv: nothing within the timeout */
    MESH_CHAN_FULL = 3,    /* try_send: a bounded channel is full */
    MESH_CHAN_CLOSED = -1, /* send: the receiver is gone; recv: every sender is gone and nothing is left */
};

/* A channel of `item`-byte messages; bound 0: unbounded. One sender and the
 * receiver are counted at creation; the caller holds one reference. */
mesh_chan *mesh_chan_new(size_t item, size_t bound, void (*drop)(void *item));
void mesh_chan_retain(mesh_chan *c);
void mesh_chan_release(mesh_chan *c);

/* Sender bookkeeping (Rust's Sender clone / drop). */
void mesh_chan_add_sender(mesh_chan *c);
void mesh_chan_drop_sender(mesh_chan *c);
/* The receiver is gone: later sends fail (and queued messages are dropped). */
void mesh_chan_close_rx(mesh_chan *c);

/* Blocks while a bounded channel is full. */
int mesh_chan_send(mesh_chan *c, const void *item);
int mesh_chan_try_send(mesh_chan *c, const void *item);
/* timeout_ms < 0: wait forever. */
int mesh_chan_recv(mesh_chan *c, void *out, int64_t timeout_ms);
int mesh_chan_try_recv(mesh_chan *c, void *out);
size_t mesh_chan_len(mesh_chan *c);

/* Milliseconds on the monotonic clock (for deadlines). */
int64_t mesh_mono_ms(void);
/* Microseconds on the monotonic clock. */
int64_t mesh_mono_us(void);
/* Sleeps (EINTR resumed). */
void mesh_sleep_us(int64_t us);

#endif
