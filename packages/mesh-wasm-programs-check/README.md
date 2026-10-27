# @ajax/mesh-wasm-programs-check

Checks every wasm guest program of alpha-c-mesh (`programs/*-wasm`) in meshd's wasm executor
(`@ajax/mesh-exec-wasm`), through `@ajax/mesh-exec-wasm-testkit`, against the CPU references
(`@ajax/mesh-ref-*`, `@ajax/mesh-ftz`): the port of the Rust's kcheck W checks and wasm lifecycle
expectations for each program. Private: tests only.

The guest modules are found beside the test binary (`.sexc/out/<profile>/wasm/`), so build first:
`sexc build && sexc test`. (sexc can't yet make a host package depend on a wasm package.)
