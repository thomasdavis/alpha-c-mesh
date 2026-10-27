# @ajax/mesh-driver

meshd's driver thread: the only ring publisher and program-table writer; lifecycle, blinks, publishing, pipes and job items.

meshd's driver thread (driver.rs).

The only publisher of every ring and the only writer of the program table. Each tick it collects completions, honours run requests and commands, and publishes the next tasks of every loaded program -- each its own resident kernel (or wasm lanes, or a native worker) with its own ring.

Lifecycle (plan §3.1): start/stop launch or halt one program's kernel and touch nothing else (zero gap). Anything that changes the context's module set (load, upgrade, unload --now) is a *blink*, because module loads and unloads block while any kernel of the context runs: drain every ring, halt every kernel, do the module operations (images compiled beforehand on a worker thread), rewind each ring head to its published tail, launch everything that runs, then wait for all of it. Rollback and reset need no module operation: they blink only their slot. Isolated executors (mps, the CPU executors) blink only the slot a module change touches.

Executors are per slot (mesh-abi PX_EXEC): the GPU backend and beside it the CPU executors (wasm lanes, native worker processes). A load runs on the first executor its manifest's `backends` lists that this meshd has.

```c
#include "mesh/driver.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/driver.rs (all of it: `Config`, `Job`, `Op`, `Blink`, `Waiter`, `Zombie`, `GoneInbox`, `DeadCell`, `JLease`, `JobSlot`, `SetupState`, `Driver` and every method), and programs.rs `Image`, `Phase`, `Slot`, `view_compatible`.

## Tests

- `tests/driver.c`: driver.rs's `job_setup_in_order` and `reads_by_name`; and the driver end to end over a fake CPU executor whose nodes are a thread playing mesh-rt's node loop: autoload (and a program it may not run), the blink, OP_INIT, tasks, refusals, stop and start, reset, `--state`, unload and load into the freed slot, shutdown.
- apps/meshd's test runs it with the real wasm and native executors.

## Differences

- **Placement (place.rs) is not wired**: no placer is ever created (as in a single meshd, or a program without `place`), so `place` answers "declares no `place`" when peered and F_PLACE/F_FRAME from members are logged and dropped. The fleet area's @ajax/mesh-placer provides the Placer; wiring it means `sync_place`, `place_acts`, `apply_mode`, `place_tick`, `place_frame` and the role checks in `publish_slot`, `ProgRun`, `Reset` and `Input`.
- The JIT runs one detached thread per load or upgrade, as the Rust's `std::thread::spawn`.

## Licence

MIT. See the repository's LICENSE.
