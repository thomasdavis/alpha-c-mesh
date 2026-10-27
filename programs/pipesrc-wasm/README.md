# @ajax/mesh-prog-pipesrc-wasm

pipesrc: the pipe demo's source on meshd's wasm executor, pushing msgs to pipehash or digests straight to a sink.

The wasm image of the `pipesrc` program for meshd's wasm executor, built by sexc as a `"kind": "wasm"`
package on `@ajax/mesh-rt-wasm` (at -O2, as the Rust builds its guests release). Ported from
alpha-rust-mesh's `programs/pipesrc/wasm/src/lib.rs`: the same view layout, ops and results as the program's other images. In a
catalog it is `<name>.wasm` beside `program.toml` (the GPU programs and the manifest are
`programs/pipesrc`, where one exists).

Tests: `@ajax/mesh-wasm-programs-check` runs it in the executor against the CPU references.
