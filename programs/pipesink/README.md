# @ajax/mesh-prog-pipesink

The pipe demo's checking sink, a native worker program: it drains its inbox of `digest` records,
checks each against its own SHA-256d of the msg rebuilt from the seq, counts GPU- and CPU-hashed ones,
and histograms the latency since the source stamped the msg (1 us bins, p50/p90/p99).

- **Ported from:** alpha-rust-mesh `programs/pipesink/src/main.rs`.
- **Tests:** on @ajax/mesh-worker-node's loop with a test mailbox: 40 digests (3 corrupted) drained and
  checked, the counters, the histogram and the percentiles, an empty inbox, and reset. They currently
  skip the drain checks with a message: @ajax/mesh-worker-ctx's `mesh_ctx_pipe_len` treats
  `mesh_pipe_target`'s success (0) as a failure. With that one-character fix applied, they all pass.
