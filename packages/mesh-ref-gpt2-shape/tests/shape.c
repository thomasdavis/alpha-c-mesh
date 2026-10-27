/* gpt2.rs's layout_covers_the_checkpoint, and the tensor names, offsets and counts against a golden
 * digest from the Rust. */
#include "mesh/ref/gpt2_shape.h"

#include <stdint.h>
#include <stdio.h>

int main(void) {
    mesh_gpt2_layout l;
    mesh_gpt2_layout_init(&l);
    int failures = l.total != 124439808;
    uint64_t h = 0xcbf29ce484222325ull;
    size_t at = 0, i = 0;
    char name[32];
    size_t off, n;
    for (; mesh_gpt2_tensor(&l, i, name, &off, &n); i++) {
        if (off != at) failures++;
        at += n;
        for (const char *c = name; *c; c++) h = (h ^ (uint8_t)*c) * 0x100000001b3ull;
        h = (h ^ off) * 0x100000001b3ull;
        h = (h ^ n) * 0x100000001b3ull;
    }
    if (i != MESH_GPT2_TENSORS || at != l.total || h != 0x3ec233629c7428b2ull) {
        fprintf(stderr, "%zu tensors, %zu floats, digest %#018llx\n", i, at, (unsigned long long)h);
        failures++;
    }
    return failures != 0;
}
