# @ajax/mesh-abi-pipe-hash

Pipe hashes: a record's position-mixed sum and an inbox type's FNV-1a 32.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/pipe_hash.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2/pipe.rs (sum_part, rec_sum, pos_mix, rec_sum_at, type_hash).

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here.

## Tests

- `tests/golden.c`: its functions against 76 vectors computed by the Rust on the same inputs

## Licence

MIT. See the repository's LICENSE.
