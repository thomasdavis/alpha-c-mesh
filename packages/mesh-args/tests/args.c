/* args.rs's defaults, full, backends and errors, one for one. */
#define _XOPEN_SOURCE 700 /* sexc compiles tests without the package's cflags (reported) */
#include "mesh/args.h"
#include "mesh/check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The CUDA flags are tested as `--backend cuda` (the default on Linux). */
static int p(const char *line, mesh_args *a, char **err) {
    char buf[1024];
    if (strstr(line, "--backend")) snprintf(buf, sizeof buf, "%s", line);
    else snprintf(buf, sizeof buf, "--backend cuda %s", line);
    char *argv[64];
    int argc = 0;
    for (char *t = strtok(buf, " "); t; t = strtok(NULL, " ")) argv[argc++] = t;
    *err = NULL;
    return mesh_args_parse(argc, argv, a, err);
}

static int bad(const char *line, const char *contains) {
    mesh_args a;
    char *err;
    int r = p(line, &a, &err);
    mesh_args_free(&a);
    int ok = r == -1 && (!contains || (err && strstr(err, contains)));
    if (!ok) fprintf(stderr, "accepted or wrong error for %s: %s\n", line, err ? err : "-");
    free(err);
    return ok;
}

int main(void) {
    mesh_args a;
    char *err;
    CHECK_I(p("", &a, &err), 0);
    CHECK_S(a.control, "127.0.0.1:7700");
    CHECK_S(a.http, "0.0.0.0:8088");
    CHECK(!a.cluster);
    CHECK_S(a.nvjitlink, "/usr/local/cuda/lib64");
    CHECK_S(a.programs, "out/programs");
    CHECK(!a.state && !a.autoload);
    mesh_args_free(&a);
    CHECK_I(p("--nvjitlink /opt/x", &a, &err), 0);
    CHECK_S(a.nvjitlink, "/opt/x");
    mesh_args_free(&a);
    CHECK_I(p("--programs /p --state /s --autoload", &a, &err), 0);
    CHECK(!strcmp(a.programs, "/p") && !strcmp(a.state, "/s") && a.autoload);
    mesh_args_free(&a);

    CHECK_I(p("--sms 24 --control 127.0.0.1:17700 --http 127.0.0.1:18080 --system 0 --peer 127.0.0.1:17701 --fleet-state f", &a, &err), 0);
    CHECK(a.has_sms && a.sms == 24 && a.has_system && a.system == 0);
    mcc_topology t = mcc_pair_dialer("127.0.0.1:17701");
    CHECK(a.cluster && mcc_topology_eq(&a.topo, &t));
    CHECK_S(a.jobs_state, "f.jobs");
    CHECK_S(a.blobs, "f.blobs");
    mcc_topology_free(&t);
    mesh_args_free(&a);
    CHECK_I(p("--system 1 --peer-listen 0.0.0.0:7701 --fleet-state f", &a, &err), 0);
    t = mcc_pair_listener("0.0.0.0:7701");
    CHECK(mcc_topology_eq(&a.topo, &t));
    mcc_topology_free(&t);
    mesh_args_free(&a);
    CHECK(bad("--system 1 --peer-listen 0.0.0.0:7701", "--fleet-state"));
    CHECK(bad("--fleet-state f", NULL));
    char path[64];
    snprintf(path, sizeof path, "/tmp/meshd-args-%d.conf", (int)getpid());
    FILE *f = fopen(path, "w");
    fputs("systems = 3\n0.peer = h:1\n1.peer = h:2\n2.peer = g:3\n", f);
    fclose(f);
    char line[256];
    snprintf(line, sizeof line, "--cluster %s --system 1 --fleet-state f", path);
    CHECK_I(p(line, &a, &err), 0);
    CHECK(a.topo.me == 1 && a.topo.n == 3 && a.topo.listen && !strcmp(a.topo.listen, "h:2") && a.topo.ndial == 1 && a.topo.dial_k[0] == 2 &&
          !strcmp(a.topo.dial_addr[0], "g:3"));
    CHECK(a.has_keys && !strcmp(ms_get(&a.keys, "2.peer"), "g:3"));
    mesh_args_free(&a);
    snprintf(line, sizeof line, "--cluster %s --system 3 --fleet-state f", path);
    CHECK(bad(line, NULL));
    snprintf(line, sizeof line, "--cluster %s", path);
    CHECK(bad(line, "--cluster needs --system K"));
    snprintf(line, sizeof line, "--cluster %s --peer h:1 --system 0", path);
    CHECK(bad(line, "--peer: --cluster given already (one of --cluster, --peer, --peer-listen)"));
    unlink(path);
    CHECK(bad("--cluster /nonexistent/x --system 0", "--cluster /nonexistent/x: No such file or directory (os error 2)"));

    CHECK_I(mesh_args_parse(0, NULL, &a, &err), 0);
    CHECK_U(a.backend, MESH_DEFAULT_BACKEND);
    mesh_args_free(&a);
    CHECK_I(p("--backend metal", &a, &err), 0);
    CHECK_U(a.backend, MESH_BACKEND_METAL);
    mesh_args_free(&a);
    CHECK_I(p("--backend cuda --sms 8", &a, &err), 0);
    CHECK_U(a.backend, MESH_BACKEND_CUDA);
    mesh_args_free(&a);
    CHECK(bad("--backend vulkan", "--backend \"vulkan\": cuda, metal or wasm"));
    CHECK(bad("--backend", "--backend needs a value"));
    const char *cuda[5] = {"--sms 8", "--nvjitlink /x", "--isolation mps", "--cuda-lib /x", "--mps-pool 2"};
    for (int i = 0; i < 5; i++) {
        snprintf(line, sizeof line, "--backend metal %s", cuda[i]);
        CHECK(bad(line, "CUDA flag"));
    }
    CHECK(bad("--backend wasm --sms 8", "--sms: a CUDA flag; --backend wasm has no such setting (no silent ignoring)"));
    CHECK_I(p("--backend metal --programs /p", &a, &err), 0);
    CHECK_S(a.programs, "/p");
    mesh_args_free(&a);

    CHECK(bad("--sms", "--sms needs a value"));
    CHECK(bad("--sms 0", "--sms must be > 0"));
    CHECK(bad("--sms x", "--sms: bad number \"x\""));
    CHECK(bad("--bogus", "unknown flag \"--bogus\"\nusage: meshd"));
    const char *gone[5] = {"--ptx x.ptx", "--entry mesh", "--nodes 4", "--run train", "--lr 0.1"};
    for (int i = 0; i < 5; i++) CHECK(bad(gone[i], NULL));
    CHECK(bad("--system 0", "--system needs --cluster FILE"));
    CHECK(bad("--system 1 --peer h:1", "--peer is for --system 0"));
    CHECK(bad("--system 0 --peer-listen h:1", "--peer-listen is for --system 1"));
    CHECK(bad("--isolation green", "--isolation \"green\": blink or mps"));
    CHECK(bad("--mps-dir /tmp/x", "--mps-dir and --mps-pool are for --isolation mps"));
    CHECK(bad("--mps-pool 2", NULL));
    CHECK(bad("--help", "usage: meshd [--backend"));
    CHECK_I(p("--isolation mps --mps-pool 2", &a, &err), 0);
    CHECK(a.has_mps_pool && a.mps_pool == 2);
    mesh_args_free(&a);
    CHECK_I(p("--isolation mps --mps-dir /tmp/x", &a, &err), 0);
    CHECK(a.isolation == MESH_ISOLATION_MODE_MPS && !strcmp(a.mps_dir, "/tmp/x"));
    mesh_args_free(&a);
    CHECK_I(p("", &a, &err), 0);
    CHECK(a.isolation == MESH_ISOLATION_MODE_BLINK);
    mesh_args_free(&a);
    return mesh_check_done();
}
