# @ajax/mesh-samplers

meshd's two once-a-second samplers: the load telemetry (machine and programs) and the network interfaces.

meshd's two sampler threads.

- load (load.rs `sampler`): every second, this machine's host sample and
every loaded program's (mesh-load-host, mesh-load-prog) into the host
block's load regions and the mailbox's LOAD_PAGE; in a fleet, the member
summary to every member (F_LOADREP) and the scheduler's report (F_PERF,
through the fleet's hook).
- net (net.rs `sampler`): every second, the interfaces that carry (or
carried) a peer link, and the total, into the host block's NIC records.

Both run until sh->quit; a host that cannot be read is said once per distinct error, and the sample's age shows it to viewers.

```c
#include "mesh/samplers.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/load.rs `sampler`; crates/mesh-host/src/net.rs `sampler`.

## Tests

- `tests/samplers.c`: both run on this machine for a few seconds: load samples, CPUs, memory, a program's record, the load page, NIC samples and the total; both stop on quit.

## Differences

- The fleet's scheduler report (`perf_report`, F_PERF) goes through the `mesh_load_perf` hook the fleet area sets; F_LOADREP is broadcast here.

## Licence

MIT. See the repository's LICENSE.
