# @ajax/mesh-infer-svc

Where each member's inference services listen, and the address one member dials for another's.

Where each member's inference services listen (infer.rs Svc): its peer host and port (the base: a worker listens on base + its manifest's `param`) and the address members outside its subnet use (`k.lan` in the cluster config).

```c
#include "mesh/infer_svc.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/infer.rs: `Svc` (single, cluster, addr), `split_hp`, `subnet24`.

## Tests

- `tests/svc.c`: infer.rs's `addresses_pick_the_shared_link`, and the error texts.

## Differences

- `cluster` reads the config through a lookup function instead of a `BTreeMap`.

## Licence

MIT. See the repository's LICENSE.
