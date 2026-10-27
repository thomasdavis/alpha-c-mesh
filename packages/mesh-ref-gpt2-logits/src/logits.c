/* The greedy pick and the logits digest (mesh-ref gpt2.rs `argmax`, `digest_extend`). */
#include "mesh/ref/gpt2_logits.h"

#include "mesh/ftz.h"

uint32_t mesh_gpt2_argmax(const float *l, size_t n) {
    size_t b = 0;
    for (size_t i = 1; i < n; i++)
        if (mesh_ftz_gt(l[i], l[b])) b = i;
    return (uint32_t)b;
}

uint64_t mesh_gpt2_digest_extend(uint64_t h, const float *l, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ mesh_ftz_bits(l[i])) * 0x00000100000001b3ull;
    return h;
}
