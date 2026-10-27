/* native.rs's limit_bits_and_macos_refusals, plus the probe and the cgroup check on this machine. */
#include "mesh/check.h"
#include "mesh/native_limits.h"

#include "mesh/abi/control_block.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    mesh_native_limits l = {200, 1u << 30, 5, true};
    CHECK_U(mesh_native_limits_bits(&l), MESH_LIMIT_CPU | MESH_LIMIT_MEM | MESH_LIMIT_NICE | MESH_LIMIT_BG);
    char *d = mesh_native_limits_debug(&l);
    CHECK_S(d, "Limits { cpu_pct: 200, mem: 1073741824, nice: 5, background: true }");
    free(d);
    char *err = NULL;
    mesh_native_limits nice_only = {0, 0, 3, false};
    CHECK(mesh_native_limits_available(&nice_only, &err));
    /* A scoped limit: works where systemd's user manager delegates, else refuses with why. */
    mesh_native_limits mem = {0, 1u << 30, 0, false};
    if (mesh_native_limits_available(&mem, &err)) printf("systemd-run --user --scope works here\n");
    else {
        CHECK(strncmp(err, "load: limits: ", 14) == 0);
        printf("scoped limits unavailable here: %s\n", err);
    }
    /* This test process is not in a scope with those limits. */
    char *e2 = NULL;
    bool in_scope = mesh_native_check_cgroup(getpid(), &mem, &e2);
    CHECK(!in_scope || e2 == NULL);
    if (!in_scope) CHECK(strncmp(e2, "limits: ", 8) == 0);
    free(e2);
    free(err);
    return mesh_check_done();
}
