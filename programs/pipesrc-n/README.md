# @ajax/mesh-prog-pipesrc-n

The pipe demo's CPU source, a native worker program (programs/pipesrc's twin): mode 0 pushes `msg`s
into pipehash's inbox, mode 1 hashes them itself and pushes `digest`s into a sink; a full pipe ends the
task (backpressure), no consumer counts as gone.

- **Ported from:** alpha-rust-mesh `programs/pipesrc-n/src/main.rs`.
- **Tests:** on @ajax/mesh-worker-node's loop: msgs in seq order on pipehash's inbox, backpressure, gone
  without a sink, digests into a sink, and the ops.
