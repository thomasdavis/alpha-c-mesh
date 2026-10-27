# @ajax/mesh-inproc

A GPU program driven in-process through meshd's backend, as kcheck checks every program: `mesh_dev` (CUDA in blink mode, or any backend), a mailbox with its header, loading a catalog dir (compile, load, `MESH_RT_ABI`, `nodes = "fit"`), installing a slot under the seqlock, launch and stop with their times, publishing, waiting, batches, the input queue, the request window, the view heap and reads masks.

## Differences

`mesh_dev_open_cuda` covers CUDA; a wasm executor is passed in with `mesh_dev_from` (Metal is not ported).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/inproc.rs.

## Tests

`tests/inproc.c`, on the fake device: a catalog dir loaded and checked, `nodes = "fit"`, reads masks, the heap, install, launch, tickets and a batch, the input queue, a request, a stop and a relaunch.

## Licence

MIT. See the repository's LICENSE.
