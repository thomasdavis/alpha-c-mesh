# @ajax/mesh-mps-worker

`meshd worker`: one program image in its own MPS client process. It maps meshd's mailbox file, registers it for the device, loads its one cubin (shape, scratch, `MESH_RT_ABI`, registers, occupancy, local bytes, the stack sized for it), launches `mesh_main` when told, and reports when the stream finished; it exits on W_QUIT, or at EOF after stopping its own kernel (meshd died).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/mps_worker.rs.

## Tests

Through @ajax/mesh-mps's test, whose MPS workers run this on the fake device.

## Licence

MIT. See the repository's LICENSE.
