/* gpt2.rs's attention_of_one_key_is_its_value. (Golden data for attention over many keys is in
 * @ajax/mesh-ref-gpt2's tests.) */
#include "mesh/ref/gpt2_attend.h"

int main(void) {
    float q[64], k[64], v[64], out[64];
    for (int i = 0; i < 64; i++) {
        q[i] = (float)i / 64.0f;
        k[i] = 1.0f - (float)i / 64.0f;
        v[i] = (float)i * 0.25f - 3.0f;
    }
    if (!mesh_gpt2_attend_row(q, 1, k, 0, v, 0, out)) return 1;
    for (int i = 0; i < 64; i++)
        if (out[i] != v[i]) return 1;
    return !mesh_gpt2_attend_row(q, 0, k, 0, v, 0, out) || mesh_gpt2_attend_row(q, 1025, k, 0, v, 0, out);
}
