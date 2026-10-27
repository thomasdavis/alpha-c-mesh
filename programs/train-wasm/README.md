# @ajax/mesh-prog-train-wasm

train for meshd's wasm executor: SGD of a cubic on sin(pi x) samples, lockstep with the cluster through the exchange page, bit for bit the GPUs' (cluster float mode).

The wasm image of the `train` program for meshd's wasm executor, built by sexc as a `"kind": "wasm"`
package on `@ajax/mesh-rt-wasm` (at -O2, as the Rust builds its guests release). Ported from
alpha-rust-mesh's `programs/train/wasm/src/lib.rs`: the same view layout, ops and results as the program's other images. In a
catalog it is `<name>.wasm` beside `program.toml` (the GPU programs and the manifest are
`programs/train`, where one exists).

Tests: `@ajax/mesh-wasm-programs-check` runs it in the executor against the CPU references.
