# The port

From [alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) at `00b526b` (2026-10-26),
~54 000 lines of Rust plus Metal, to C.

## Phases

| Phase | Area | Rust | State |
|---|---|---|---|
| 1 | The contract: mailbox, program table, control blocks, rings, view heap, snapshot, control protocol, peer frames, pipes, views, wasm ABI | `crates/mesh-abi` | in progress ([port/abi.md](port/abi.md)) |
| 1 | Cluster float mode `Z`, the CPU references, the path tracer core | `crates/mesh-ref`, `crates/pathtrace-core` | in progress ([port/ref.md](port/ref.md)) |
| 1 | sexc: header-only packages; `ptx` and `wasm` package kinds | (sexc) | in progress |
| 2 | Device runtime and every GPU program, C → PTX | `crates/mesh-rt`, `programs/*` | planned |
| 2 | Wasm: an interpreter in C (replacing wasmtime), the guest runtime, the wasm programs | `mesh-host/wasm.rs`, `crates/mesh-rt-wasm`, `programs/*/wasm` | planned |
| 2 | meshd core: CUDA driver and nvJitLink (dlopen), the mailbox, loading, lifecycle and blink, control port, HTTP relay, MPS, native workers | `crates/mesh-host`, `crates/mesh-worker` | planned |
| 2 | The fleet: peers, membership, leader, desired state, reconcile, scheduler, placement, network | `crates/mesh-host` | planned |
| 2 | Jobs and blobs | `crates/mesh-host` | planned |
| 2 | mesh-top, a terminal UI in C, and its mock meshd | `crates/mesh-top` | planned |
| 2 | Checks and tools: kcheck, lifecheck, fleetcheck; the scripts as C programs | `crates/mesh-kcheck`, `mesh-lifecheck`, `mesh-fleetcheck`, `scripts/` | planned |
| 3 | Publish every package to sexc.dev | | planned |

## Not ported (yet)

- **The Apple Metal backend** (`metal.rs`, `lease.rs`, `crates/mesh-rt-metal`, `programs/*/program.metal`).
  Metal Shading Language is C++, not C, and there is no Mac here to test on.
- **`programs/ane`** (the Apple Neural Engine).

## Verification

There is no GPU on the machine this port is built on. Everything that can run on a CPU is tested
against the Rust: golden vectors generated from the Rust crates, and the Rust unit tests ported. GPU
programs are compiled to PTX in every test run, and the same C sources are compiled for the CPU and
checked against the references. Running the fleet on real GPUs (the GB10s, the RTX 3060) is a separate
step.
