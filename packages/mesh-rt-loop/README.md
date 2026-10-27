# @ajax/mesh-rt-loop

`mesh_rt_run(mb, slot, init, task)`: the node loop, the body of every program's kernel. Thread 0 of the
node takes a ticket from the slot's ring, waits for its publication, hands the descriptor to the block
through shared memory and a barrier; the block runs the op; thread 0 completes it (result, mark released,
`P_DONE`, the node record, the trace record). Idle, it keeps the heartbeat and the node load record (time
slices the context did not get), takes no ticket while `H_BLINK` is set, and leaves at a task boundary
once `P_SHUTDOWN` is set.

- **Ported from:** alpha-rust-mesh `crates/mesh-rt/src/ring.rs`, statement for statement (including the
  one-wide-multiply-add indexing that works around ptxas 13.0 on sm_121).
- **Tests:** @ajax/mesh-prog-hello runs batches over two nodes, a halt and a relaunch on the simulator,
  checking results, marks, `P_DONE`, `P_ONLINE` and counters.
