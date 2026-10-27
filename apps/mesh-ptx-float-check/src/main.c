/* mesh-ptx-float-check PTX: refuses a program PTX that breaks the cluster
 * float mode (@ajax/mesh-ptx-float-rules). Exit 0 fine, 1 refused (the
 * problems on stderr), 2 usage. Ported from alpha-rust-mesh
 * scripts/ptx-float-check.sh. */
#include "mesh/ptx_float_rules.h"
#include "read.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s PTX\n", argc > 0 ? argv[0] : "mesh-ptx-float-check");
        return 2;
    }
    size_t len = 0;
    char *text = mesh_ptx_read_file(argv[1], &len);
    if (!text) {
        fprintf(stderr, "usage: %s PTX\n", argv[0]);
        return 2;
    }
    char *report = NULL;
    size_t rlen = 0;
    FILE *mem = open_memstream(&report, &rlen);
    int bad = mesh_ptx_float_check(argv[1], text, len, mem);
    fclose(mem);
    if (bad) {
        fprintf(stderr, "error: %s: float op outside the cluster float mode (FTZ, correctly rounded, unfused):\n%s", argv[1], report);
    }
    free(report);
    free(text);
    return bad ? 1 : 0;
}
