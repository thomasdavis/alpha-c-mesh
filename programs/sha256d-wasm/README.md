# @ajax/mesh-prog-sha256d-wasm

sha256d for meshd's wasm executor: Bitcoin-style double SHA-256 nonce grinding as a job program, the GPU image's view, ops and request language.

The wasm image of the `sha256d` program for meshd's wasm executor, built by sexc as a `"kind": "wasm"`
package on `@ajax/mesh-rt-wasm` (at -O2, as the Rust builds its guests release). Ported from
alpha-rust-mesh's `programs/sha256d/wasm/src/lib.rs`: the same view layout, ops and results as the program's other images. In a
catalog it is `<name>.wasm` beside `program.toml` (the GPU programs and the manifest are
`programs/sha256d`, where one exists).

Tests: `@ajax/mesh-wasm-programs-check` runs it in the executor against the CPU references.
