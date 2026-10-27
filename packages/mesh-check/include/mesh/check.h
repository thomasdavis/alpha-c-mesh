/* @ajax/mesh-check: Test checks for alpha-c-mesh's packages: CHECK, CHECK_U, CHECK_I, CHECK_S, then mesh_check_done(). */
#ifndef MESH_CHECK_H
#define MESH_CHECK_H

/* Checks for this repo's tests: each failure prints where and what, and
 * mesh_check_done() turns the tally into the exit status. Header-only. */
#include <stdio.h>
#include <string.h>

static int mesh_check_failures;
static long mesh_check_count;

#define CHECK(c) mesh_check_(!!(c), __FILE__, __LINE__, #c)
#define CHECK_U(a, b) mesh_check_u_((unsigned long long)(a), (unsigned long long)(b), __FILE__, __LINE__, #a, #b)
#define CHECK_I(a, b) mesh_check_i_((long long)(a), (long long)(b), __FILE__, __LINE__, #a, #b)
#define CHECK_S(a, b) mesh_check_s_((a), (b), __FILE__, __LINE__, #a)
#define CHECK_F32_BITS(a, bits) mesh_check_u_(mesh_check_f32_bits_(a), (bits), __FILE__, __LINE__, #a, #bits)

static inline int mesh_check_(int ok, const char *file, int line, const char *what) {
    mesh_check_count++;
    if (!ok) {
        mesh_check_failures++;
        fprintf(stderr, "%s:%d: check failed: %s\n", file, line, what);
    }
    return ok;
}
static inline int mesh_check_u_(unsigned long long a, unsigned long long b, const char *file, int line,
                                const char *ea, const char *eb) {
    mesh_check_count++;
    if (a != b) {
        mesh_check_failures++;
        fprintf(stderr, "%s:%d: %s == %s: %llu (0x%llx) != %llu (0x%llx)\n", file, line, ea, eb, a, a, b, b);
        return 0;
    }
    return 1;
}
static inline int mesh_check_i_(long long a, long long b, const char *file, int line, const char *ea, const char *eb) {
    mesh_check_count++;
    if (a != b) {
        mesh_check_failures++;
        fprintf(stderr, "%s:%d: %s == %s: %lld != %lld\n", file, line, ea, eb, a, b);
        return 0;
    }
    return 1;
}
static inline int mesh_check_s_(const char *a, const char *b, const char *file, int line, const char *ea) {
    mesh_check_count++;
    if (!a || !b || strcmp(a, b) != 0) {
        mesh_check_failures++;
        fprintf(stderr, "%s:%d: %s: \"%s\" != \"%s\"\n", file, line, ea, a ? a : "(null)", b ? b : "(null)");
        return 0;
    }
    return 1;
}
static inline unsigned long long mesh_check_f32_bits_(float f) {
    unsigned int u;
    memcpy(&u, &f, sizeof u);
    return u;
}
/* The exit status: 0 when every check passed. */
static inline int mesh_check_done(void) {
    if (mesh_check_failures) fprintf(stderr, "%d of %ld checks failed\n", mesh_check_failures, mesh_check_count);
    return mesh_check_failures ? 1 : 0;
}

#endif
