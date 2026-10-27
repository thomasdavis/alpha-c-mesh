# @ajax/mesh-host-block

meshd's host status block (S_* fields) under one lock, with its uptime clock and the live network counters.

```
@ajax/mesh-host-block: the host status block (status.rs HostStatus):
SNAP_HOST bytes, little-endian, written by meshd's threads under one lock,
read whole by the snapshot with the uptime and the live network counters
filled in at that moment (the counters' rates are timed by that uptime).
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/status.rs: `HostStatus` (new, set_u32, set_f32, set_u64, set_many, add_u32, set_str, uptime_ms, get_u32, get_f32, bytes).

## Tests

- `tests/host_block.c`: every setter, the truncated string, a consistent set, and the copy with uptime and network counters.

## Differences

- `snapshot` (status.rs) is @ajax/mesh-snapshot.

## Licence

MIT. See the repository's LICENSE.
