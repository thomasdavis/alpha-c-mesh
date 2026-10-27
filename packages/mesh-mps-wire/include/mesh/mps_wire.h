/* @ajax/mesh-mps-wire: the frames between meshd and its --isolation mps
 * worker processes (mesh-host mps.rs): a u32 LE length (op + body), the u8
 * op, its body.  On the worker's stdin and stdout. */
#ifndef MESH_MPS_WIRE_H
#define MESH_MPS_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* meshd -> worker */
#define MESH_W_LOAD 1   /* u32 threads, u32 smem (the launch shape), u32 scratch bytes, u32 name_len, the name, the cubin */
#define MESH_W_LAUNCH 2 /* u32 gen, u32 slot, u32 nodes */
#define MESH_W_QUIT 3
#define MESH_W_UNLOAD 4 /* no reply: the module goes, the context stays (pooled) */
#define MESH_W_READY 5  /* replied with W_READY, u32 ok, once the context exists */
/* worker -> meshd, same framing */
#define MESH_W_LOADED 1   /* u32 ok; ok: u32 rt_abi, regs, per_sm, local, sms, u64 mailbox dev, u64 scratch dev; else the error */
#define MESH_W_LAUNCHED 2 /* u32 ok; else the error */
#define MESH_W_DONE 3     /* u32 gen, u32 ok; else the error: the stream of launch gen finished */

/* The largest frame read_frame accepts (64 MiB). */
#define MESH_W_FRAME_MAX (64u << 20)

/* One frame: *out (malloc'd) and *n bytes. */
void mesh_mps_frame(uint8_t op, const uint8_t *body, size_t n, uint8_t **out, size_t *out_len);
/* Writes one frame to fd, whole (EINTR retried). */
bool mesh_mps_write_frame(int fd, uint8_t op, const uint8_t *body, size_t n);
/* Reads one frame from fd: *op and *body (malloc'd, *n bytes).  False at EOF,
 * on a read error, or on a frame of 0 or more than MESH_W_FRAME_MAX bytes. */
bool mesh_mps_read_frame(int fd, uint8_t *op, uint8_t **body, size_t *n);
/* The little-endian u32 at o, 0 past the end. */
uint32_t mesh_mps_u32_at(const uint8_t *b, size_t n, size_t o);
uint64_t mesh_mps_u64_at(const uint8_t *b, size_t n, size_t o);
/* Appends a little-endian u32 / u64 to a growing body. */
void mesh_mps_put_u32(uint8_t **b, size_t *n, uint32_t v);
void mesh_mps_put_u64(uint8_t **b, size_t *n, uint64_t v);
void mesh_mps_put_bytes(uint8_t **b, size_t *n, const void *p, size_t k);

#endif
