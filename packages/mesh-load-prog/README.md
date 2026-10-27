# @ajax/mesh-load-prog

One program's load sample: GPU share from its node load records, task latencies from its trace, busy time, throughput, its CPU executor's cost.

One slot's load sampler (load.rs Prog).

Each sample (about once a second) reads the program's node load records (NL_*: the GPU share its nodes obtained), node records (busy time, tasks), the trace records written since the last sample (task latencies) and P_DONE, and for a CPU executor (wasm, native) its CPU time and RSS (mesh-cpuuse), and writes the program's PLOAD record: the last second and a window of LOAD_WINDOW samples. Another image in the slot (PT_PTX_HASH, PT_LOADS) starts a fresh baseline.

```c
#include "mesh/load_prog.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/load.rs: `Nl::read`, `Prog`, `Prog::sample`, `ProgSample`.

## Tests

- `tests/load_prog.c`: load.rs's `program_sampler_reads_records_and_trace`.

## Licence

MIT. See the repository's LICENSE.
