/* @ajax/mesh-infer-wire: the wire of the inference services (programs/gpt2;
 * docs/plan-disagg-inference.md).
 *
 * Every gpt2 worker listens on TCP; frames are a u32 length (LE, of what
 * follows), a u8 kind, a payload. A request (K_GEN, K_DECODE, K_PREFILL)
 * carries a Msg: `key=value` lines (sorted by key), a blank line, a body. KV
 * travels as K_LAYER frames (layer, rows, a checksum, the bytes); K_FIRST
 * ends a prefill; K_RESULT and K_ERROR answer. */
#ifndef MESH_INFER_WIRE_H
#define MESH_INFER_WIRE_H

#include "mesh/buf.h"

#include <stddef.h>
#include <stdint.h>

#define MESH_INFER_K_GEN 1
#define MESH_INFER_K_DECODE 2
#define MESH_INFER_K_PREFILL 3
#define MESH_INFER_K_LAYER 10
#define MESH_INFER_K_FIRST 11
#define MESH_INFER_K_RESULT 20
#define MESH_INFER_K_ERROR 21
/* A frame's payload, at most (a 1024-token f32 layer is 6.3 MB). */
#define MESH_INFER_MAX_FRAME (64u << 20)
/* How long meshd waits for a request's answer (exact-cpu on a long prompt is slow): 600 s. */
#define MESH_INFER_REQUEST_WAIT_MS 600000

#define MESH_INFER_PROGRAM_MONO "gpt2"
#define MESH_INFER_PROGRAM_PREFILL "gpt2-prefill"
#define MESH_INFER_PROGRAM_DECODE "gpt2-decode"

/* Writes one frame to fd (whole; EINTR retried): 0, or -1 with errno set. */
int mesh_infer_write_frame(int fd, uint8_t kind, const void *body, size_t len);
/* Reads one frame: 0 with *kind and body (appended), and (when wire_us) the
 * microseconds the payload took after its header; -1 with *err ("read: ..."). */
int mesh_infer_read_frame(int fd, uint8_t *kind, mesh_buf *body, int64_t *wire_us, char **err);

/* key=value lines (sorted, one per key), a blank line, a body. */
typedef struct {
    char **k, **v;
    size_t n, cap;
    char *body;
} mesh_infer_msg;

void mesh_infer_msg_init(mesh_infer_msg *m);
void mesh_infer_msg_free(mesh_infer_msg *m);
void mesh_infer_msg_set(mesh_infer_msg *m, const char *k, const char *v);
void mesh_infer_msg_setf(mesh_infer_msg *m, const char *k, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void mesh_infer_msg_set_body(mesh_infer_msg *m, const char *body);
const char *mesh_infer_msg_get(const mesh_infer_msg *m, const char *k);
/* Missing / not-a-number errors as the Rust: "missing k", "k: not a number". */
int mesh_infer_msg_num(const mesh_infer_msg *m, const char *k, uint64_t *out, char **err);
void mesh_infer_msg_encode(const mesh_infer_msg *m, mesh_buf *out);
/* 0, or -1 with *err: "a message that is not UTF-8", "a header line without '=': \"..\"". */
int mesh_infer_msg_decode(const void *b, size_t n, mesh_infer_msg *out, char **err);

/* A layer's checksum: the wrapping sum of its little-endian u64 words (a short tail zero-padded). */
uint64_t mesh_infer_layer_sum(const void *b, size_t n);
/* A K_LAYER payload: layer, rows, checksum, the bytes (appended to out). */
void mesh_infer_layer_frame(mesh_buf *out, uint32_t layer, uint32_t rows, const void *bytes, size_t n);
/* A K_LAYER payload's layer, rows and bytes (pointing into b), its checksum verified. */
int mesh_infer_parse_layer(const void *b, size_t n, uint32_t *layer, uint32_t *rows, const uint8_t **bytes, size_t *len,
                           char **err);

#endif
