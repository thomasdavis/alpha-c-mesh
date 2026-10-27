# @ajax/mesh-words-host

`mesh_words` (the mailbox access interface of @ajax/mesh-abi-words) over a host mapping of the mailbox,
with C11 atomics: loads are acquire, stores release, CAS and add acq_rel, and byte copies move
little-endian words with relaxed loads and stores.

```c
#include "mesh/words_host.h"
mesh_words w = mesh_words_host(mailbox);   /* mailbox: the mapped MAILBOX_BYTES, 4-byte aligned */
mesh_pipe_push(&w, me, consumer, rec, len);
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): the host
implementations of `mesh_abi::v2::pipe::{Words, Bytes}`. Rust's pipe tests use the same orderings over a
`Vec<AtomicU32>`; the host runtimes (mesh-host's wasm lanes, mesh-worker) over the mapped mailbox.

## Tests

- `tests/words_host.c`: loads, stores, CAS, add, byte order and partial words.
- @ajax/mesh-abi-pipe-ring and @ajax/mesh-abi-pipe run the Rust's pipe tests over it.

## Device code

Host only (C11 atomics on a CPU mapping); GPU runtimes implement `mesh_words` with their own atomics.

## Licence

MIT. See the repository's LICENSE.
