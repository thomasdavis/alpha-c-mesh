# @ajax/mesh-load-proc

Parsers of the Linux /proc files the load sampler reads: stat, pressure, meminfo, loadavg, key-value files.

```
@ajax/mesh-load-proc: the pure parsers of load.rs: /proc/stat's CPU times,
the big/little core classes, PSI (the /proc/pressure files), /proc/meminfo,
/proc/loadavg and `key value` / `Key: value` files. Errors are the Rust's
text. The OS readers that feed them are mesh-load-host.
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/load.rs: `CpuTimes` (since), `parse_proc_stat`, `classify`, `class_util`, `Psi`, `parse_psi`, `psi_rate`, `MemInfo`, `parse_meminfo`, `parse_loadavg`, `field`.

## Tests

- `tests/load_proc.c`: load.rs's `proc_stat`, `cpu_rates`, `classes`, `psi_and_meminfo`, with the error texts.

## Differences

- Per-CPU results are fixed arrays of `MESH_MAX_CPUS` with a presence flag, where the Rust has `Vec<Option<_>>`.

## Licence

MIT. See the repository's LICENSE.
