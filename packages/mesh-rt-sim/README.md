# @ajax/mesh-rt-sim

mesh-rt's device primitives on the CPU, and a launcher that runs a kernel's nodes as POSIX threads, so a
mesh GPU program written in C can be tested without a GPU.

- A node is `warps` x 32 threads with its own zeroed shared memory (`RT_SHARED_BYTES` + the program's).
- The block barrier is a `pthread_barrier` over the node's threads. Every warp collective (shuffles,
  ballot, `bar.warp.sync`) is an exchange through a per-warp array between two barriers over its 32
  lanes, so programs that follow mesh-rt's rules (all lanes call warp collectives, all threads call block
  ones) behave as on a GPU.
- Loads, stores, atomics and fences are C11 atomics with the same orders; the global timer is
  `CLOCK_MONOTONIC`; `trap` aborts with the node and thread.
- Each simulated thread runs with f32 flush-to-zero (x86 MXCSR FTZ|DAZ, AArch64 FPCR.FZ), the PTX `.ftz`
  semantics of the cluster float mode.

```c
mesh_sim_launch *l = mesh_sim_start(mesh_main, mailbox, slot, nodes, warps, shared_bytes);
/* publish tickets, wait for marks (@ajax/mesh-rt-sim-mailbox) */
mesh_sim_join(l);
```

- **Ported from:** the semantics of alpha-rust-mesh `crates/mesh-rt/src/ptx.rs` (the Rust has no CPU twin of
  the GPU runtime; its native workers, `crates/mesh-worker`, run a different SDK).
- **Tests:** every program under `programs/` runs its tasks on it and compares against the CPU references.
- **Differences:** x86's FTZ/DAZ also applies to f64, which PTX never flushes: a program whose f64 values
  become subnormal would differ here (none of the ported programs does). Divergent warp collectives (not
  allowed by mesh-rt's rules) deadlock here rather than misbehave.
