# @ajax/mesh-prog-pipehash

The pipe demo's GPU stage: a warp pops up to 32 `msg` records from its inbox (a CPU source feeds it),
hashes them a lane each (sha256d) and pushes the `digest`s into a CPU sink, one collective push each.
Digests a full sink does not take are carried in the view and retried first; without room for 32 the
warp pops nothing (backpressure up the pipe). Layout: @ajax/mesh-abi-view-pipedemo.

- **Ported from:** alpha-rust-mesh `programs/pipehash/src/lib.rs`.
- **Tests:** on the simulator between a host source and a host sink (pipes through
  @ajax/mesh-words-host): no sink -> nothing done; 200 msgs in, each digest out exactly once and equal to
  the reference digest; backpressure with the sink's GPU side full, the msgs waiting in the inbox.
