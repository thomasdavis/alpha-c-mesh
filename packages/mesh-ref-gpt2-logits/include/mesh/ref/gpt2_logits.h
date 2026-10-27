/* @ajax/mesh-ref-gpt2-logits: what is done with GPT-2's logits (mesh-ref gpt2.rs `argmax`,
 * `digest_extend`): the greedy pick and one digest over every step of a generation. Freestanding. */
#ifndef MESH_REF_GPT2_LOGITS_H
#define MESH_REF_GPT2_LOGITS_H

#include <stddef.h>
#include <stdint.h>

#define MESH_GPT2_DIGEST_SEED 0xcbf29ce484222325ull

/* The first index of the largest logit (FTZ compare). */
uint32_t mesh_gpt2_argmax(const float *l, size_t n);
/* FNV-1a 64 over the logits' bit words, continued from h (start: MESH_GPT2_DIGEST_SEED). */
uint64_t mesh_gpt2_digest_extend(uint64_t h, const float *l, size_t n);

#endif
