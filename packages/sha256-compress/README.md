# @ajax/sha256-compress

One SHA-256 compression function and the initial state, freestanding C.

```c
#include "sha256_compress.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2/views.rs (pipedemo::compress and its constants).

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here.

## Tests

- none of its own: it is exercised by the packages that use it

## Licence

MIT. See the repository's LICENSE.
