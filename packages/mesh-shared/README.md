# @ajax/mesh-shared

meshd's shared state between its threads, and the messages they send the driver, the fleet thread and the jobs thread.

State shared by meshd's threads, and the messages they send the driver (commands), the fleet thread (events) and the jobs thread (shared.rs).

The driver thread is the only ring publisher and program-table writer: every other thread asks it through `cmd` and waits on a one-shot reply. In a fleet every lifecycle change comes from the reconciler (the committed desired state); the control port goes through the leader (`change`).

Payloads that belong to the fleet and jobs areas (placement and fleet messages, links, beats, blob lists) travel as opaque pointers with their free function: this package only carries them.

```c
#include "mesh/shared.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/shared.rs: `Src`, `Cmd`, `ViewBytes`, `PeerEvent`, `JobEvent`, `JobReply`, `Shared` (`new`, `send_peer`, `send_peer_wait`, `send_bulk`, `send_mask`, `broadcast`, `fail_stop`, `ask`, `set_peer_state`, `change`, `http`), `mono_us`, `PEER_*`; fleet.rs `Actual` as the inventory record.

## Tests

- `tests/shared.c`: asking the driver (answered, refused, dropped unanswered, timed out, with the Rust's texts), the http relay's counting, link queues (bounded, broadcast to the linked ones, waiting for room), fail-stop.

## Differences

- The fleet's and the jobs' own messages and state travel as opaque pointers with their free functions (`msg`, `fleet_ext`, `jobs_ext`): those areas own them. The texts other threads refresh are one lock's fields.

## Licence

MIT. See the repository's LICENSE.
