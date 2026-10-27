# @ajax/cuda-driver-fake

A simulated CUDA device behind @ajax/cuda-driver's table: device attributes, primary and green contexts, "cubins" (a line of text naming the entry, registers, local bytes, thread limit and `MESH_RT_ABI`), globals, opt-in shared memory, scratch memory, streams, and launches whose kernels are C functions run on a thread of their own while the stream reads busy. Device memory is host memory, so a fake kernel can play a program's node loop against the real mailbox. Failure injection (`mesh_fake_cuda_fail_next`) and counters of everything it saw.

Not a port: it exists so the CUDA, MPS and in-process backends are tested without a GPU.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): nothing (test support).

## Tests

Exercised by @ajax/mesh-cuda's, @ajax/mesh-mps's and @ajax/mesh-inproc's tests.

## Licence

MIT. See the repository's LICENSE.
