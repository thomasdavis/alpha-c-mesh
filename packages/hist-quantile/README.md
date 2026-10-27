# @ajax/hist-quantile

The per-mille quantile of a histogram of unit bins.

```c
#include "hist_quantile.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2/views.rs (pipedemo::quantile).

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here.

## Tests

- `tests/golden.c`: its functions against 54 vectors computed by the Rust on the same inputs

## Licence

MIT. See the repository's LICENSE.
