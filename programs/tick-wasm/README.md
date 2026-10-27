# @ajax/mesh-prog-tick-wasm

tick for meshd's wasm executor: a pattern region of the view ticked one word a task and verified by sampling.

The wasm image of the `tick` program for meshd's wasm executor, built by sexc as a `"kind": "wasm"`
package on `@ajax/mesh-rt-wasm` (at -O2, as the Rust builds its guests release). Ported from
alpha-rust-mesh's `programs/tick/wasm/src/lib.rs`: the same view layout, ops and results as the program's other images. In a
catalog it is `<name>.wasm` beside `program.toml` (the GPU programs and the manifest are
`programs/tick`, where one exists).

Tests: `@ajax/mesh-wasm-programs-check` runs it in the executor against the CPU references.
