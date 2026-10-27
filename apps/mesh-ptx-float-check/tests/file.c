/* The app's file reading, and a whole conforming and a bad PTX through the rules. */
#include "mesh/check.h"
#include "mesh/ptx_float_rules.h"

#include "../src/read.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    char path[] = "/tmp/mesh-ptx-float-check-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    const char *bad = ".version 7.1\n.target sm_86\n\tfma.rn.f32 %r1, %r2, %r3, %r4;\n";
    CHECK(write(fd, bad, strlen(bad)) == (ssize_t)strlen(bad));
    close(fd);
    size_t len = 0;
    char *t = mesh_ptx_read_file(path, &len);
    CHECK(t && len == strlen(bad));
    char *report = NULL;
    size_t rl = 0;
    FILE *m = open_memstream(&report, &rl);
    CHECK_I(mesh_ptx_float_check(path, t, len, m), 1);
    fclose(m);
    char want[256];
    snprintf(want, sizeof want, "%s:3: fused multiply-add: \tfma.rn.f32 %%r1, %%r2, %%r3, %%r4;\n", path);
    CHECK_S(report, want);
    free(report);
    free(t);
    unlink(path);
    CHECK(mesh_ptx_read_file("/nonexistent/x.ptx", &len) == NULL);
    return mesh_check_done();
}
