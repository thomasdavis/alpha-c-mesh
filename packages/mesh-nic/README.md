# @ajax/mesh-nic

Network interface counters: /proc/net/dev (or macOS's 64-bit if_data), wrap-safe totals, NIC records.

The machine's network interfaces' counters (net.rs, "Network I/O").

Read once a second: Linux /proc/net/dev, macOS the routing sysctl's 64-bit if_data64. Each OS counter is extended to a monotonic u64 (a narrower one that went down wrapped; a 64-bit one that went down was reset), and a total sums every physical interface (mesh-abi NET_VIRTUAL prefixes left out) that never goes down. NIC records (NI_*) are what the host block publishes.

```c
#include "mesh/nic.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/net.rs: `NIC_COUNTERS`, `Raw`, `Ext`, `parse_proc_net_dev`, `read_interfaces` (Linux and macOS), `iface_of`, `Nics`, `nic_record`.

## Tests

- `tests/nic.c`: net.rs's `proc_net_dev`, `ext_wraps_and_resets`, `total_is_monotonic_and_skips_virtual`, `loopback_address_is_on_lo`, `this_machine_has_interfaces`, `nic_record_layout`.

## Differences

- The macOS reader (`NET_RT_IFLIST2`) is ported but untested: there is no Mac here.
- `Nics::sample`'s map is returned as an array in the OS's order, looked up by name (`mesh_nic_lookup`); the Rust returns a `BTreeMap`.

## Licence

MIT. See the repository's LICENSE.
