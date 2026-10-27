# @ajax/mesh-load-text

The control port's load request: this machine's load and every program's, as key value lines.

The control port's `load` request (load.rs `text`): this machine's load (GPU share, CPUs by class, PSI, memory, meshd, the CPU executors, disk, thermal) and each loaded program's, as `key value` lines, read from the host status block the load sampler fills.

```c
#include "mesh/load_text.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/load.rs: `text`.

## Tests

- `tests/load_text.c`: the text of an empty sample, and of a machine (GPU share, cores, PSI) and a wasm program with figures.

## Licence

MIT. See the repository's LICENSE.
