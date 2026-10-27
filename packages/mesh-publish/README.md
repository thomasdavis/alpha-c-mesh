# @ajax/mesh-publish

The generic task publisher: one ticket ring per program slot, and the manifest's policy deciding what meshd publishes.

The generic task publisher (publish.rs; contract v2).

One ring per program slot, and the manifest's policy deciding what meshd publishes on its own: continuous (a batch whenever the previous one completed, or topped up to `fanout` in flight with in_flight = "free"), periodic (`fanout` tasks at `hz`), on_demand (nothing; only task/req). The arg is the index 0..fanout or the view's u32 at `arg_off` (+ index). Nothing here knows any program: a new program needs no meshd change.

Ticket rules: ticket t uses ring entry and completion slot t % 64; t may be published only once no outstanding ticket is 64 or more behind.

```c
#include "mesh/publish.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/publish.rs: `Kind`, `Out`, `PRing` (every method), `due`, `publish_auto`, `publish_parts`.

## Tests

- `tests/publish.c`: publish.rs's `rings_are_per_slot`, `full_ring_and_rewind` and `policies`, one for one.

## Differences

- `Instant`s are nanoseconds on the monotonic clock.

## Licence

MIT. See the repository's LICENSE.
