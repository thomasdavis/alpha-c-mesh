# @ajax/mesh-worker-node

mesh-rt's node loop on CPU threads: claim the next published ticket (E_SEQ acquire, then a CAS on P_RING_HEAD), run `init` (OP_INIT) or `task`, complete it (P_RESULTS, P_MARKS release, P_DONE, the node record, a trace record), heartbeat while idle with a backoff that spins first and sleeps longer the longer it waits, and halt at P_SHUTDOWN. `mesh_worker_run_nodes` runs a slot's PT_NODES nodes, a thread each.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-worker/src/lib.rs (`node_loop`, `run_nodes`, `idle`).

## Tests

`tests/node.c`: the Rust's `nodes_run_the_ring_like_mesh_rt` (4 nodes, 200 tickets, a bad op, results, counters, node records, traces), `faults_requests_and_peers` and `input_queue_pops_once_each`.

## Licence

MIT. See the repository's LICENSE.
