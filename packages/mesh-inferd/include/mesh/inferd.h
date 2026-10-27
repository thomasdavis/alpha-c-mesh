/* @ajax/mesh-inferd: meshd's side of an inference request (inferd.rs).
 *
 * The pair: explicit (`prefill=K decode=J`, `mono=K`), or the members where
 * the fleet's desired state placed gpt2-prefill and gpt2-decode (this member's
 * committed copy, read from the `fleet` text), or a monolithic gpt2. Then one
 * call to the decode (or monolithic) member's worker, which answers K_RESULT:
 * the tokens, the text, the logits digest, the timings. */
#ifndef MESH_INFERD_H
#define MESH_INFERD_H

#include "mesh/infer_request.h"
#include "mesh/infer_wire.h"
#include "mesh/shared.h"

/* One request: 0 and *res (free it with mesh_infer_msg_free), or -1 and *err. */
int mesh_inferd_run(mesh_shared *sh, const mesh_infer_request *r, mesh_infer_msg *res, char **err);
/* The members a program runs on: the desired state's `at` (all: every member), or 0 when single and loaded here. */
size_t mesh_inferd_members_of(mesh_shared *sh, const char *name, uint32_t *out);

#endif
