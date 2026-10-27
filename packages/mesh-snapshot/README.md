# @ajax/mesh-snapshot

Snapshot v3: the mailbox header, one section per loaded program, and the host block, in one frame for viewers.

The snapshot sent to viewers (status.rs `snapshot`; mesh-abi "snapshot v3"): a header, the mailbox header, one section per non-empty slot in ascending order (its table entry, control block, extension, node records and its view's snapshot prefix, zero padded), then the host block with the load regions.

```c
#include "mesh/snapshot.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/status.rs: `snapshot`.

## Tests

- `tests/snapshot.c`: status.rs's `layout`.

## Licence

MIT. See the repository's LICENSE.
