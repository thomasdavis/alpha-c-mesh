# @ajax/mesh-rt-sim-mailbox

A mailbox in host memory with one mesh program slot, and the host side of the contract for it, for tests
on @ajax/mesh-rt-sim: the header of a single system, the program table entry (under its seqlock), the
extension (param, scratch), publishing tickets and waiting for their marks (as meshd's publisher), the
request window, the input queue, and a halt that rewinds the ring for a relaunch.

```c
uint8_t *mb = mesh_sim_mailbox();
mesh_sim_prog p = {.name = "hello", .view_bytes = 0x1000, .nodes = 2};
uint8_t *view = mesh_sim_install(mb, 3, &p);
mesh_sim_launch *l = mesh_sim_launch_prog(mb, 3, &p, mesh_main);
uint32_t r = mesh_sim_call(mb, 3, MESH_OP_TASK, 5);
mesh_sim_halt(mb, 3, l);
```

- **Ported from:** the publisher rules of alpha-rust-mesh `crates/mesh-host/src/publish.rs` (ticket t uses
  ring entry and completion slot t % 64; no ticket 64 or more ahead of the oldest outstanding one) and the
  halt rewind (`PRing::rewind`). Not a meshd: no policies, blinks or snapshots.
- **Tests:** every program's simulator test uses it.
