/* Reading the golden files the Rust generator wrote (data/): floats as bit patterns, strings with
 * spaces as hex, expected outputs as (length, FNV-1a 64). */
#ifndef GOLDEN_H
#define GOLDEN_H

#include "mesh/abi/fnv.h"
#include "mesh/check.h"
#include "mesh/parse.h"
#include "mesh/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) exit(1);
    b[n] = 0;
    fclose(f);
    *len = (size_t)n;
    return b;
}

static inline float f32_bits(const char *h) {
    uint32_t u = (uint32_t)strtoul(h, NULL, 16);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static inline double f64_bits(const char *h) {
    uint64_t u = strtoull(h, NULL, 16);
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static inline char *unhex(const char *h) {
    if (strcmp(h, "~") == 0) return mt_strdup("");
    size_t n = strlen(h) / 2;
    char *s = mt_alloc(n + 1);
    for (size_t i = 0; i < n; i++) {
        char b[3] = {h[2 * i], h[2 * i + 1], 0};
        s[i] = (char)strtoul(b, NULL, 16);
    }
    return s;
}

/* Does text match the expected "len hash" words? */
static inline bool matches(const char *text, const char *len, const char *hash) {
    size_t n = strlen(text);
    char h[24];
    snprintf(h, sizeof h, "%016llx", (unsigned long long)mesh_fnv1a64((const uint8_t *)text, n));
    return (size_t)strtoull(len, NULL, 10) == n && strcmp(h, hash) == 0;
}

#endif
