# AGENTS.md

Read this before changing anything. It applies to people and agents alike.

This repo is a port to C of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard
Safier, MIT): AlphaOS's mesh of resident GPU programs, its daemon `meshd`, the fleet, jobs, pipes, the
wasm and native executors, `mesh-top` and the checks. It is built with [sexc](https://sexc.dev), and
every package is published to the sexc registry as `@ajax/<name>`.

## The rules

1. **Every single line of code is C.** Host code, GPU kernels (C compiled to PTX by clang), wasm guest
   programs (C compiled to wasm32 by clang), tools, tests and build helpers. No shell scripts, no Rust, no
   Python, no CUDA C++, no Makefiles. A script in the Rust repo becomes a small C program here.
2. **Atomic packages.** A package does one thing and is small enough to read in one sitting. If a
   package's description needs "and", split it. Prefer many tiny packages with precise dependencies over
   one big one: a constant table, a ring buffer, a parser, a protocol, one scheduler policy, one program.
   Each package has a README.md saying what it is, what it is ported from (Rust file and item), and how
   it is tested.
3. **Faithful.** Behaviour, wire formats, memory layouts, constants, error text and numerics match the
   Rust implementation exactly (bit-for-bit where the Rust is bit-for-bit, e.g. the cluster float mode
   `Z`). Where the Rust has tests, port them. Where the Rust has a reference implementation, test
   against vectors generated from it. If the C must differ, say so in that package's README under
   "Differences".
4. **Publishable.** Every package must build on its own from the registry: flags it needs
   (`-D_GNU_SOURCE`, `-D_XOPEN_SOURCE=700`, `-lm`, `-ldl`, `-lpthread`) go in **its own** `sexc.json`
   `cflags`/`ldflags`, because the root's flags are not published. Fill `description`, `license: "MIT"`,
   `repository`, `keywords`. Version `0.1.0` to start.

## Names and layout

| Kind | Folder | Package name |
|---|---|---|
| Libraries | `packages/<name>/` | `@ajax/<name>` e.g. `@ajax/mesh-abi-mailbox` |
| GPU/wasm/native programs | `programs/<name>/` | `@ajax/mesh-prog-<name>` |
| Executables (meshd, mesh-top, checks, tools) | `apps/<name>/` | `@ajax/<name>` |

C identifiers: public names are prefixed with the package's short name in snake_case (`mesh_abi_mailbox_`
or a shorter agreed prefix noted in its README); constants keep their Rust names with a `MESH_` prefix
(`MESH_H_MAGIC`, `MESH_PT_STATE`). Headers live in `include/<pkg-short-name>.h` or
`include/mesh/<thing>.h`; never two packages with the same header path.

## Building

```
sexc build            # every package
sexc test             # every test
sexc test --profile asan
```

`sexc` is at https://sexc.dev (`cc -o bootstrap bootstrap.c && ./bootstrap` in the sexc repo). C17,
`-Wall -Wextra -Wpedantic -Werror`, clean on gcc and clang. No GPU is needed to build or test: device
programs also compile for the CPU and are checked against the CPU references; PTX is checked to build.

## Porting notes

- The Rust repo is checked out at `/mnt/donto-data/workspace/ports/alpha-rust-mesh` (read only; never
  edit it). Its toolchain: `cargo +1.88.0` (for generating reference vectors from the CPU crates).
- docs/port/*.md track what each area has ported and how the Rust maps onto C packages.
