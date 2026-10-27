# @ajax/mesh-prog-sha-verify-wasm

sha-verify: re-hashes sha256d's shares on the CPU beside the GPU, reading its view through reads.

The wasm image of the `sha-verify` program for meshd's wasm executor, built by sexc as a `"kind": "wasm"`
package on `@ajax/mesh-rt-wasm` (at -O2, as the Rust builds its guests release). Ported from
alpha-rust-mesh's `programs/sha-verify/wasm/src/lib.rs`: the same view layout, ops and results as the program's other images. In a
catalog it is `<name>.wasm` beside `program.toml` (the GPU programs and the manifest are
`programs/sha-verify`, where one exists).

Tests: `@ajax/mesh-wasm-programs-check` runs it in the executor against the CPU references.
