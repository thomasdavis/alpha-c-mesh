# @ajax/mesh-control

meshd's control port: one request line, one reply (a line or a framed body), every request byte for byte as the Rust.

The control port (control.rs; mesh-abi "control protocol").

One text line per request, a text line back (`ok ...` / `err ...`), except the framed replies (u32 LE length + bytes): `snap`, `programs`, `catalog`, `req`, `peek`, `log`, `trace`, `http`, `cluster`, `load`, `fleet`, `groups`, `sched`, `sched explain <name>`, `sched dry-run`, `jobs`, `job status`, `job results`. In a fleet every lifecycle change (load, upgrade, rollback, unload, start, stop, reset, place, the lockstep program's tasks, `group ...`) is a change of the desired state: it goes to the leader, which commits it and answers once the reachable members reconciled it.

A request line is at most 4096 bytes, at most 32 clients are served at once, a client idle for 120 s (or not taking a reply within 10 s) is closed.

```c
#include "mesh/control.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/control.rs: `serve`, `client`, `client_idle`, `handle` (every request), `request`, `peek`, `inbox_counts`, `pipes`, `view`, `pipes_recs`, `programs`, `cluster`, `int`, and the reply shapes.

## Tests

- `tests/control.c`: over TCP against a stand-in driver: every reply shape and refusal, `programs`, `peek`, `req` bodies, the views (text and JSON), `cluster`, `snap`, `trace`, `log`, `render` and `jobs` without their threads, the 4096-byte line limit; control.rs's `an_idle_client_is_closed`.

## Differences

- `render` asks the jobs area's blob thread through the `mesh_control_render` hook (NULL: "the blob thread is gone").
- Request words split on ASCII whitespace (Rust's `split_whitespace` also splits on Unicode spaces).

## Licence

MIT. See the repository's LICENSE.
