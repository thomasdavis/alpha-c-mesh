# @ajax/mesh-prog-pathtrace-wasm

pathtrace for meshd's wasm executor: tiles of the deterministic path tracer in the cluster float mode, bit for bit a GPU's tile.

The wasm image of the `pathtrace` program for meshd's wasm executor, built by sexc as a `"kind": "wasm"`
package on `@ajax/mesh-rt-wasm` (at -O2, as the Rust builds its guests release). Ported from
alpha-rust-mesh's `programs/pathtrace/wasm/src/lib.rs`: the same view layout, ops and results as the program's other images. In a
catalog it is `<name>.wasm` beside `program.toml` (the GPU programs and the manifest are
`programs/pathtrace`, where one exists).

Tests: `@ajax/mesh-wasm-programs-check` runs it in the executor against the CPU references.
