/* @ajax/mesh-infer-request: an `infer` request (infer.rs Request, parse,
 * from_query, text): `infer [fast|fast16|exact|exact-cpu] [max N]
 * [prefill=K decode=J | mono=K] [repeat=R] [len=N] <prompt>` on the control
 * port, `GET /infer?prompt=..&max=..&mode=..` on meshd's http. */
#ifndef MESH_INFER_REQUEST_H
#define MESH_INFER_REQUEST_H

#include "mesh/buf.h"
#include "mesh/infer_wire.h"

#include <stdbool.h>
#include <stdint.h>

#define MESH_INFER_USAGE                                                                                               \
    "usage: infer [fast|fast16|exact|exact-cpu] [max N] [prefill=K decode=J | mono=K] [repeat=R (the prompt R times)] " \
    "[len=N (cut to N tokens)] <prompt>"

typedef struct {
    char mode[16];
    uint32_t max;
    bool has_prefill, has_decode, has_mono, has_len, has_repeat;
    uint32_t prefill, decode, mono, len, repeat;
    char *prompt;
} mesh_infer_request;

/* The words after `infer`: 0, or -1 with *err (free it). Free r with mesh_infer_request_free. */
int mesh_infer_parse(const char *arg, mesh_infer_request *r, char **err);
/* GET /infer's query: the request and whether format=full. */
int mesh_infer_from_query(const char *q, mesh_infer_request *r, bool *full, char **err);
void mesh_infer_request_free(mesh_infer_request *r);
/* The control port's text: `key value` lines, then `text <the generated text>`. */
void mesh_infer_text(mesh_buf *out, const mesh_infer_msg *res);
/* %XX and '+' decoded; the bytes read as UTF-8 lossily (infer.rs url_decode). */
char *mesh_infer_url_decode(const char *s);

#endif
