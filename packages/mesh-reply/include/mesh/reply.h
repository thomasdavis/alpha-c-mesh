/* @ajax/mesh-reply: one-shot replies, as meshd's Rust used a Sender<Result<..>>
 * per request (shared.rs Reply, TaskReply, HttpReply).
 *
 * A reply is a mesh_chan of one mesh_result. The requester makes it, hands a
 * sender reference to whoever answers (mesh_reply_sender), waits with a
 * timeout, and releases its own reference. The answerer sends once
 * (mesh_reply_send and friends consume its reference) or drops it unanswered
 * (mesh_reply_drop), which the waiter sees at once as MESH_CHAN_CLOSED. A
 * reply that arrives after the waiter gave up is freed with the channel. */
#ifndef MESH_REPLY_H
#define MESH_REPLY_H

#include "mesh/chan.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ok(text) / Ok(value) / Ok(bytes), or Err(text). Owns text and bytes. */
typedef struct {
    bool ok;
    uint32_t value;
    char *text;     /* the Ok text (Reply), or the error; NULL when neither */
    uint8_t *bytes; /* the Ok bytes (HttpReply) */
    size_t len;
} mesh_result;

typedef mesh_chan mesh_reply;

mesh_reply *mesh_reply_new(void);
/* The requester's handle for an answerer (retained; the answerer consumes it). */
mesh_reply *mesh_reply_sender(mesh_reply *r);
/* A second answerer handle (Rust's Sender::clone). */
mesh_reply *mesh_reply_clone(mesh_reply *r);

/* Answer and let go (r may be NULL: nobody waits). Each takes ownership of nothing but r. */
void mesh_reply_send(mesh_reply *r, mesh_result res); /* takes res's text and bytes */
void mesh_reply_ok(mesh_reply *r, const char *text);
void mesh_reply_okf(mesh_reply *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void mesh_reply_err(mesh_reply *r, const char *text);
void mesh_reply_errf(mesh_reply *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void mesh_reply_value(mesh_reply *r, uint32_t v);
void mesh_reply_bytes(mesh_reply *r, const void *p, size_t n);
/* Let go without answering: the waiter sees MESH_CHAN_CLOSED. */
void mesh_reply_drop(mesh_reply *r);

/* The requester: waits (timeout_ms < 0: forever) and fills out (free it with
 * mesh_result_free). MESH_CHAN_OK, MESH_CHAN_TIMEOUT or MESH_CHAN_CLOSED. */
int mesh_reply_wait(mesh_reply *r, int64_t timeout_ms, mesh_result *out);
/* The requester's poll: MESH_CHAN_OK, MESH_CHAN_EMPTY or MESH_CHAN_CLOSED. */
int mesh_reply_poll(mesh_reply *r, mesh_result *out);
/* The requester lets go of its own reference. */
void mesh_reply_release(mesh_reply *r);

mesh_result mesh_result_ok(const char *text);
mesh_result mesh_result_err(const char *text);
/* A copy (text and bytes duplicated). */
mesh_result mesh_result_copy(const mesh_result *r);
void mesh_result_free(mesh_result *r);

#endif
