# @ajax/mesh-prog-hashjob-wasm

hashjob: a CPU job program (job arrays) for meshd's wasm executor: per item, folded sha256d of LE8(i * K + j).

The wasm image of the `hashjob` program for meshd's wasm executor, built by sexc as a `"kind": "wasm"`
package on `@ajax/mesh-rt-wasm` (at -O2, as the Rust builds its guests release). Ported from
alpha-rust-mesh's `programs/hashjob/wasm/src/lib.rs`: the same view layout, ops and results as the program's other images. In a
catalog it is `<name>.wasm` beside `program.toml` (the GPU programs and the manifest are
`programs/hashjob`, where one exists).

Tests: `@ajax/mesh-wasm-programs-check` runs it in the executor against the CPU references.
