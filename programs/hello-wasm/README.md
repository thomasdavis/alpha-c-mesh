# @ajax/mesh-prog-hello-wasm

hello for meshd's wasm executor: task = arg * 3 + 7, plus the wasm executor's test hooks (per-access cost, bulk reads, trap, spin, memory growth).

The wasm image of the `hello` program for meshd's wasm executor, built by sexc as a `"kind": "wasm"`
package on `@ajax/mesh-rt-wasm` (at -O2, as the Rust builds its guests release). Ported from
alpha-rust-mesh's `programs/hello/wasm/src/lib.rs`: the same view layout, ops and results as the program's other images. In a
catalog it is `<name>.wasm` beside `program.toml` (the GPU programs and the manifest are
`programs/hello`, where one exists).

Tests: `@ajax/mesh-wasm-programs-check` runs it in the executor against the CPU references.
