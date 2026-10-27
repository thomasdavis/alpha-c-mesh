# @ajax/mesh-args

meshd's command line: every flag, its default and its refusal, as the Rust parses them.

meshd's command line (args.rs; hand-rolled: a dozen flags do not need a parser). Errors are the Rust's, word for word; the CUDA-only flags are refused under another backend (no silent ignoring).

```c
#include "mesh/args.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/args.rs: `Args`, `Peer`, `USAGE`, `CUDA_FLAGS`, `backend_of`, `parse`; backend.rs `DEFAULT_BACKEND`.

## Tests

- `tests/args.c`: args.rs's `defaults`, `full`, `backends` and `errors`, one for one, with every error text.

## Differences

- The cluster config is the fleet area's parser (@ajax/mesh-cluster-conf, `mcc_*`); `Peer::Cluster(Topology)` is `cluster` plus `topo`.

## Licence

MIT. See the repository's LICENSE.
