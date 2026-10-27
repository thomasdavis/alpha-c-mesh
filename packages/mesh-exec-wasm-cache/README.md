# @ajax/mesh-exec-wasm-cache

meshd's `--wasm-cache DIR`, from alpha-rust-mesh's `mesh-host/src/wasm.rs` (`Rt::module`, `evict`): a
compiled module is kept as `<wasm fnv1a64>-<engine hash>.cwasm`, written through a temp file and a
rename, and the directory is kept under a cap (`CACHE_CAP`, 256 MiB) by deleting the oldest `.cwasm`
files first.

## Differences

What a file holds is the executor's business (`@ajax/mesh-exec-wasm`): wasmtime stores Cranelift's
machine code there; the C executor's interpreter stores the module's bytes, so a hit saves no compile
time — the cache keeps meshd's flag, file layout, logs ("compiled" / "cached") and eviction identical.

Tests: `tests/cache.c` (the cache half of the Rust's `engine_config_and_cache_eviction`).
