/* Every probe on the manifests' real shapes. */
#include "mesh/catalog_toml.h"
#include "mesh/check.h"

int main(void) {
    const char *gpu = "# hello: arg * 3 + 7\nname       = \"hello\"\nabi        = 6\nbackends   = [\"cuda\", \"metal\", \"wasm\"]\n";
    const char *native = "# hash-also: crates/mesh-host programs/gpt2x\n# build-os: Darwin\nname=\"ane\"\nbackends    = [\"native\"]\nnodes = 1\n";
    const char *spaced = "name = \"x\"\nbackends = [ \"native\" ]  # comment\n";
    const char *none = "name = \"tick-big\"\n";
    char n[64];
    CHECK(mesh_catalog_toml_name(gpu, n, sizeof n));
    CHECK_S(n, "hello");
    CHECK(mesh_catalog_toml_name(native, n, sizeof n));
    CHECK_S(n, "ane");
    CHECK(!mesh_catalog_toml_name("abi = 6\n", n, sizeof n));
    CHECK(!mesh_catalog_toml_name("names = \"x\"\n", n, sizeof n));
    CHECK(!mesh_catalog_toml_native(gpu));
    CHECK(mesh_catalog_toml_native(native));
    CHECK(mesh_catalog_toml_native(spaced));
    CHECK(!mesh_catalog_toml_native("backends = [\"native\", \"wasm\"]\n"));
    CHECK(mesh_catalog_toml_has_backends(gpu));
    CHECK(!mesh_catalog_toml_has_backends(none));
    CHECK(mesh_catalog_toml_backend(gpu, "wasm"));
    CHECK(mesh_catalog_toml_backend(gpu, "cuda"));
    CHECK(!mesh_catalog_toml_backend(native, "cuda"));
    CHECK(!mesh_catalog_toml_backend(gpu, "was"));
    char c[128];
    CHECK(mesh_catalog_toml_comment(native, "hash-also", c, sizeof c));
    CHECK_S(c, "crates/mesh-host programs/gpt2x");
    CHECK(mesh_catalog_toml_comment(native, "build-os", c, sizeof c));
    CHECK_S(c, "Darwin");
    CHECK(!mesh_catalog_toml_comment(gpu, "build-os", c, sizeof c));
    CHECK_S(c, "");
    return mesh_check_done();
}
