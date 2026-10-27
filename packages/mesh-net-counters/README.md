# @ajax/mesh-net-counters

meshd's live network counters: bytes and frames per peer link and kind, the control port's and the HTTP relay's.

meshd's live network counters (mesh-abi "Network I/O").

Per peer link, the bytes and frames by kind meshd reads and writes; the control port's and the HTTP relay's bytes and requests. Every counter is a cumulative u64 since meshd started, a relaxed atomic bumped once per frame or request by the thread that moved it; `fill` copies them into a host status block at snapshot time (viewers derive rates).

```c
#include "mesh/net_counters.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/net.rs: `LinkCounters`, `Counters` (rx, tx, ctl, http, fill).

## Tests

- `tests/net_counters.c`: net.rs's `counters_fill_the_block`.

## Licence

MIT. See the repository's LICENSE.
