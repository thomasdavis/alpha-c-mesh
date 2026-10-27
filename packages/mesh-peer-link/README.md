# @ajax/mesh-peer-link

The fleet's TCP links: `mpn_connect` (bounded in time: a dead peer would otherwise take minutes of SYN
retries), `mpn_handshake` (F_JOIN both ways, checked), `mpn_listen`, an acceptor (each lower system's
link handshaken on its own thread) and dialers (redialing with a 100 ms .. 1 s backoff once their link
dies, refusals logged once). A link (`mpn_link_start`) has a reader thread that reads whole frames by
type, refusing any hostile length before it reads it (`mpn_read_frame`: exactly peer.rs's reader
rules), and a writer thread that sends every queued control frame before each bulk frame, so a blob in
flight delays a beat by one frame at most. The lockstep rounds write directly under the writer lock
(`mpn_write_now`).

The owner gets frames and the close on the link's reader thread (`mpn_sink`); it must not free the link
from that callback (queue it to its own thread).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) `crates/mesh-host/src/peer.rs`:
`connect`, `handshake`, `acceptor`, `dialer`, `Link`, `reader` (its framing; the decoding is the
relay's), `writer`, `LINK_QUEUE`, `BULK_QUEUE`.

## Tests

`tests/link.c`: the Rust's `handshake_over_tcp`, every frame type read back through the framer, hostile
lengths refused, and a link's frames both ways with its close.

## Licence

MIT. See the repository's LICENSE.
