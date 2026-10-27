/* loader.rs's state-file half of confined_and_catalog. */
#include "mesh/check.h"
#include "mesh/state_file.h"
#include "mesh/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char dir[] = "/tmp/meshd-state-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char *st = mesh_fmt("%s/state", dir);
    mesh_saved *set;
    size_t n;
    char *err = NULL;
    CHECK(mesh_state_read(st, &set, &n, &err) && n == 0);
    mesh_saved want[] = {{"hello", "hello-v2", true}, {"x", "x", false}};
    CHECK(mesh_state_write(st, want, 2, &err));
    CHECK(mesh_state_read(st, &set, &n, &err));
    CHECK_U(n, 2);
    CHECK_S(set[0].name, "hello");
    CHECK_S(set[0].dir, "hello-v2");
    CHECK(set[0].running && !set[1].running);
    mesh_state_free(set, n);
    FILE *f = fopen(st, "w");
    fputs("hello ../x 1\n", f);
    fclose(f);
    CHECK(!mesh_state_read(st, &set, &n, &err));
    char *want_err = mesh_fmt("--state %s line 1: expected `name dir 0|1`, got \"hello ../x 1\"", st);
    CHECK_S(err, want_err);
    free(want_err);
    free(err);
    unlink(st);
    free(st);
    rmdir(dir);
    return mesh_check_done();
}
