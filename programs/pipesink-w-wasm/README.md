# @ajax/mesh-prog-pipesink-w-wasm

pipesink-w: the pipe demo's sink on meshd's wasm executor: drains digests, checks each against sha256d, histograms latency.

The wasm image of the `pipesink-w` program for meshd's wasm executor, built by sexc as a `"kind": "wasm"`
package on `@ajax/mesh-rt-wasm` (at -O2, as the Rust builds its guests release). Ported from
alpha-rust-mesh's `programs/pipesink-w/wasm/src/lib.rs`: the same view layout, ops and results as the program's other images. In a
catalog it is `<name>.wasm` beside `program.toml` (the GPU programs and the manifest are
`programs/pipesink-w`, where one exists).

Tests: `@ajax/mesh-wasm-programs-check` runs it in the executor against the CPU references.
