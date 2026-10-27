/* Reading a whole file into memory. */
#include "read.h"

#include <stdio.h>
#include <stdlib.h>

char *mesh_ptx_read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    char *b = malloc(cap + 1);
    while (b) {
        size_t r = fread(b + n, 1, cap - n, f);
        n += r;
        if (n < cap) break;
        cap *= 2;
        char *nb = realloc(b, cap + 1);
        if (!nb) { free(b); b = NULL; }
        else b = nb;
    }
    int err = ferror(f);
    fclose(f);
    if (!b || err) { free(b); return NULL; }
    b[n] = 0;
    *len = n;
    return b;
}
