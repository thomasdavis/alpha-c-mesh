# @ajax/mesh-http-relay

meshd's HTTP port: request heads relayed to the GPU http program, and the routes meshd answers itself (the views, infer, render).

The HTTP port (http.rs).

The host only moves bytes: routing and responses are the GPU http program's (`req http` through its request window), except the fleet's views `/fleet`, `/jobs` and `/pipes`, GPT-2 inference (`/infer?prompt=..`) and a fleet-rendered image (`/render?job=..`, `/render.bmp?job=..`, `/render.ppm?job=..`), which meshd answers itself -- also where the http program cannot run. A request head must arrive within 2 s; at most 64 clients are served at once.

```c
#include "mesh/http_relay.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/http.rs: `serve`, `read_head`, `view_route`, `render_route`, `client`.

## Tests

- `tests/http.c`: http.rs's `view_routes` (and the render routes); over TCP: a view in text and JSON, a bad format (400), a head relayed to a stand-in http program, the 503 when it cannot answer, render without a blob thread, infer without a prompt; the connection counters.

## Differences

- `render` goes through @ajax/mesh-control's `mesh_control_render` hook (the jobs area's blob thread).

## Licence

MIT. See the repository's LICENSE.
