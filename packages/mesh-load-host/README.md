# @ajax/mesh-load-host

This machine's load sample: the OS readers (CPU, PSI, memory, disk, thermal), rates between samples, the machine region and the member summary.

```
@ajax/mesh-load-host: this machine's side of the load telemetry (load.rs):

- `mesh_host_read`: one sample of the host (per-CPU times, load average,
PSI, memory, swap, meshd's own CPU, RSS and threads, free disk for the
catalog, the hottest thermal zone), from Linux's /proc and /sys;
- `mesh_host_derive`: rates between two samples (per-CPU utilisation by core
class, iowait, PSI shares, meshd's CPU, swap-outs);
- `mesh_machine_bytes`: the host block's machine region (S_LOAD..S_MLOAD);
- `mesh_exec_totals`: the CPU executors' sums into it;
- `mesh_loadrep`: the member summary sent to the cluster as F_LOADREP.
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/load.rs: `Host`, `os::core_classes`, `os::host`, `os::has_psi`, `self_cpu_us`, `disk`, `Derived`, `derive`, `machine_bytes`, `exec_totals`, `loadrep`.

## Tests

- `tests/load_host.c`: load.rs's `machine_region_and_loadrep` and `this_host_samples`, and the executors' totals.

## Differences

- The macOS reader (IOKit core classes, host_statistics64, memorystatus, thermal state) is not ported: there is no Mac here; `mesh_host_read` says so on other systems.
- `exec_totals` takes the native workers' and the log's counts as arguments instead of reading another module's statics.

## Licence

MIT. See the repository's LICENSE.
