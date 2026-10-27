# @ajax/mesh-abi-words

Word and byte access to the mailbox at mailbox offsets: the interface pipes run over.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/words.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2/pipe.rs (the Words and Bytes traits).

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, `<stdatomic.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here.

## Tests

- none of its own: it is exercised by the packages that use it

## Differences

Rust's `Words`/`Bytes` traits are a table of function pointers here (`mesh_words`). A missing `fence` means `atomic_thread_fence(memory_order_seq_cst)`, the trait's default. `core::hint::spin_loop()` has no C equivalent and is left out (the loops are bounded by TRIES either way).

## Licence

MIT. See the repository's LICENSE.
