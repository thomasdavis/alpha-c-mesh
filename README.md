# alpha-c-mesh

AlphaOS's mesh, in C: GPU programs that run as resident CUDA kernels that never exit, a daemon (`meshd`)
that loads, starts, stops, upgrades and rolls them back at runtime, CPU programs beside them (wasm and
native), typed pipes between programs, and a fleet of up to 8 machines with a leader, a scheduler, job
arrays and lockstep groups.

This is a port of [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) by Richard Safier, and
it follows that design and its measurements exactly. Two things are different:

- **Everything is C.** Host code, the GPU kernels (C compiled to PTX by clang, as the Rust compiled
  `no_std` Rust to PTX), the wasm guest programs (C compiled to wasm32), the tools and the tests.
- **Everything is a small package.** Each piece is its own [sexc](https://sexc.dev) package, published
  to the sexc registry as `@ajax/<name>`, so you can take just the mailbox layout, just the ticket ring,
  just the scheduler, or just one program.

Status and the map from the Rust crates to C packages: [docs/PORT.md](docs/PORT.md).

## Build

```
sexc build
sexc test
```

Get sexc from https://sexc.dev. No GPU is needed to build or test: the device programs also compile
for the CPU and are checked against the CPU references, and their PTX is checked to build.

## Licence

MIT, as the Rust original. See [LICENSE](LICENSE).
