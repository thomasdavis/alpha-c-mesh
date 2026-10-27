/* The cache's names, its atomic store and load, and eviction oldest first (the Rust's
 * engine_config_and_cache_eviction, the cache half). */
#define _XOPEN_SOURCE 700
#include "mesh/check.h"
#include "mesh/exec_wasm_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int main(void) {
    char dir[] = "/tmp/meshd-wasm-cache-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char nested[256];
    snprintf(nested, sizeof nested, "%s/a/b", dir);
    char *err = NULL;
    CHECK(mesh_wasm_cache_dir(nested, &err));
    const uint8_t wasm[] = {0, 'a', 's', 'm', 1, 0, 0, 0};
    char *p = mesh_wasm_cache_path(nested, wasm, sizeof wasm, 0x1234);
    CHECK(strstr(p, "-0000000000001234.cwasm") != NULL);
    CHECK_U(strlen(strrchr(p, '/') + 1), 16 + 1 + 16 + 6);
    uint8_t blob[1000];
    memset(blob, 7, sizeof blob);
    CHECK(mesh_wasm_cache_store(p, blob, sizeof blob, &err));
    uint8_t *back;
    size_t n;
    CHECK(mesh_wasm_cache_load(p, &back, &n));
    CHECK_U(n, sizeof blob);
    CHECK(memcmp(back, blob, n) == 0);
    free(back);
    /* A newer file; over the cap by one byte: the oldest (the module) goes, the newest stays. */
    struct timespec ms20 = {0, 20000000};
    nanosleep(&ms20, NULL);
    char newer[512];
    snprintf(newer, sizeof newer, "%s/new.cwasm", nested);
    uint8_t big[4096] = {0};
    CHECK(mesh_wasm_cache_store(newer, big, sizeof big, &err));
    CHECK_U(mesh_wasm_cache_evict(nested, sizeof blob + sizeof big - 1, NULL), 1);
    struct stat st;
    CHECK(stat(p, &st) != 0);
    CHECK(stat(newer, &st) == 0);
    /* Under the cap: nothing goes. */
    CHECK_U(mesh_wasm_cache_evict(nested, 1u << 20, NULL), 0);
    /* A file where the directory should be. */
    char file[512];
    snprintf(file, sizeof file, "%s/file", dir);
    FILE *f = fopen(file, "w");
    fclose(f);
    CHECK(!mesh_wasm_cache_dir(file, &err));
    CHECK(err && strncmp(err, "--wasm-cache ", 13) == 0);
    free(err);
    remove(newer);
    remove(file);
    char sub[512];
    snprintf(sub, sizeof sub, "%s/a", dir);
    rmdir(nested);
    rmdir(sub);
    rmdir(dir);
    free(p);
    return mesh_check_done();
}
