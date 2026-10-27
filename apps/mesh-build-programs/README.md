# @ajax/mesh-build-programs

```
mesh-build-programs [-C DIR] [name...]
```

Builds every mesh program under `programs/` (or the named ones) into the catalog meshd loads from,
`out/programs/<name>/`:

| Image | How | Checked |
|---|---|---|
| `<name>.ptx` | `sexc build --release` of the program's `ptx` package, then `mesh-ptx-link` on its bitcode (the cluster float mode, only `mesh_main` and `MESH_RT_ABI` exported) | `.target <arch>` exactly (no `debug`), the float rules (@ajax/mesh-ptx-float-rules), the `mesh_main` entry, `MESH_RT_ABI` |
| `<name>.wasm` | the program's `wasm` package (`programs/<name>-wasm`, from the wasm builder) | the `mesh_init`, `mesh_task`, `mesh_rt_abi` exports |
| `<triple>/<name>`, `source.hash` | a native program's `executable` package; `# build-os:` limits it to one OS | the source identity (@ajax/mesh-source-hash) |
| `program.toml` | copied | its `name` must be the folder's |

Then each `programs/<name>/variants/<v>.toml` gets `out/programs/<v>/` with the same images under its
name. Everything lands by rename, so a running meshd never reads half a file.

Environment: `MESH_PROGRAMS_OUT` (the catalog), `MESH_PTX_ARCH` (default `sm_86`: meshd JITs it for the
GPU it runs on), `MESH_WASM_ONLY=1`, `MESH_NATIVE_ONLY=1`, `MESH_SKIP_WASM=1`, `SEXC`, `MESH_PTX_LINK`.

- **Ported from:** alpha-rust-mesh `scripts/build-programs.sh`.
- **Differences:**
  - Images come from sexc packages, not cargo.
  - A wasm image comes from a `programs/<name>-wasm` package; the Rust pinned wasm builds to one rustc,
    and here clang builds them.
  - `MESH_SKIP_WASM=1` is new, so a catalog can be built for CUDA and native members before the wasm
    images exist.
  - Native programs build with sexc's `release` profile; the Rust added `-C target-cpu=native`.
  - There is no `program.metal`, because the Metal backend is not ported.
- **Tests:**
  - package ids, the arch rule and sexc.json fields;
  - on temporary trees with no tools: a wasm-only program with its variant, a native program for another
    OS (manifest and source hash, no binary), a GPU program failing without its tools, and a wrong arch,
    variant name and manifest name all refused.
  - The whole catalog was built with it on this machine (12 GPU images, 3 native, 7 variants).
