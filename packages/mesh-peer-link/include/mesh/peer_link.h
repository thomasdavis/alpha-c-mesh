/* @ajax/mesh-peer-link: the fleet's links (peer.rs): a full mesh of TCP links, system k listening
 * (k > 0) and dialing every j > k. Each link opens with F_JOIN both ways and then lives on its own:
 * a reader thread reads whole frames (refusing a hostile length before reading it) and hands each to
 * the owner, and a writer thread sends what is queued, every control frame before each bulk frame. A
 * link that received nothing for dead_ms closes; its dialer redials (backoff 100 ms .. 1 s). */
#ifndef MESH_PEER_LINK_H
#define MESH_PEER_LINK_H

#include "mesh/peer_codec.h"
#include "mesh/place_wire.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define MPN_LINK_QUEUE 1024 /* control frames queued per link before new ones are dropped */
#define MPN_BULK_QUEUE 64   /* bulk frames (blobs) queued per link */

/* A TCP connect bounded in time; -1 with *err ("connect host:port: why"). */
int mpn_connect(const char *addr, uint32_t timeout_ms, char **err);
/* JOIN both ways on a fresh socket (has_expect: the system dialed); sets TCP_NODELAY and the dead time
 * as the socket's read and write timeouts. */
bool mpn_handshake(int fd, uint32_t me, uint32_t n, const mpk_caps *caps, bool has_expect, uint32_t expect, uint32_t dead_ms, mpk_join *out, char **err);
/* Listens on addr ("host:port"); -1 with *err. */
int mpn_listen(const char *addr, char **err);

/* The total length of a frame given its first bytes: reads through read(ctx, buf, n) (false: the
 * socket's error in *err) and returns the whole frame (type byte first). Exactly the reader's rules. */
typedef bool (*mpn_read_fn)(void *ctx, uint8_t *buf, size_t n, char **err);
bool mpn_read_frame(mpn_read_fn read, void *ctx, mpw_bytes *frame, char **err);

typedef struct mpn_link mpn_link;

/* What a link reports to its owner (called on the link's reader thread). */
typedef struct {
    void *ctx;
    /* A whole frame from member from on link lid (type byte first; takes it). */
    void (*frame)(void *ctx, uint32_t from, uint64_t lid, mpw_bytes frame);
    /* The link closed (why: "closed", "silent for N ms", or the read or decode error). */
    void (*closed)(void *ctx, uint32_t from, uint64_t lid, const char *why);
    /* Bytes counted (net counters): tx when written, rx when a frame was read whole. */
    void (*count)(void *ctx, uint32_t k, bool tx, uint8_t type, size_t bytes);
} mpn_sink;

/* Starts a JOINed link's reader and writer threads (takes fd). alive: the dialer's flag (NULL for an
 * accepted link), cleared when the link dies so the dialer redials. */
mpn_link *mpn_link_start(int fd, uint32_t from, uint64_t lid, uint32_t dead_ms, const mpn_sink *sink, atomic_bool *alive);
uint32_t mpn_link_from(const mpn_link *l);
uint64_t mpn_link_lid(const mpn_link *l);
bool mpn_link_alive(const mpn_link *l);
/* Queues a frame; false when the queue is full (it is dropped) or the link is dead. Copies f. */
bool mpn_send(mpn_link *l, const uint8_t *f, size_t n);
bool mpn_send_bulk(mpn_link *l, const uint8_t *f, size_t n);
/* Writes a frame now, under the writer lock (the lockstep rounds' path). */
bool mpn_write_now(mpn_link *l, const uint8_t *f, size_t n);
/* Closes the socket (a blocked send returns); the threads end. */
void mpn_link_close(mpn_link *l);
/* Closes, joins the threads and frees. */
void mpn_link_free(mpn_link *l);

/* Accepts the lower systems' links on a listening socket, handshakes each on its own thread, and hands
 * the JOINed socket over (refusals are logged). */
typedef struct {
    void *ctx;
    void (*linked)(void *ctx, int fd, const mpk_join *join, atomic_bool *alive);
    void (*log)(void *ctx, const char *line);
} mpn_owner;

typedef struct mpn_acceptor mpn_acceptor;
mpn_acceptor *mpn_acceptor_start(int listen_fd, uint32_t me, uint32_t n, const mpk_caps *caps, uint32_t dead_ms, const mpn_owner *owner);
void mpn_acceptor_stop(mpn_acceptor *a);

/* Keeps a link to system j (> me) up: dial, JOIN, hand over, redial once *alive goes false (the owner
 * clears it when the link dies). */
typedef struct mpn_dialer mpn_dialer;
mpn_dialer *mpn_dialer_start(uint32_t j, const char *addr, uint32_t me, uint32_t n, const mpk_caps *caps, uint32_t dead_ms, const mpn_owner *owner);
void mpn_dialer_stop(mpn_dialer *d);

#endif
