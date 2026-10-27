# @ajax/mesh-chan

A channel of fixed-size messages between threads with the semantics meshd's Rust relied on
(`std::sync::mpsc`): many senders and one receiver, messages in send order, unbounded or bounded
(`try_send` refuses when full, as `sync_channel`), sends fail once the receiver is gone, receives report
"disconnected" once every sender is gone and nothing is queued, and receives can wait forever, for a
timeout on the monotonic clock, or not at all. A reference count keeps the channel alive for its last
holder; messages left behind go to an optional `drop` function so what they own is freed.

Also `mesh_mono_ms`, `mesh_mono_us` and `mesh_sleep_us` (the monotonic clock the deadlines use).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): meshd's threads talk
through `std::sync::mpsc` channels (`crates/mesh-host/src/shared.rs`: `Cmd`, `PeerEvent`, `JobEvent`, the
link queues); this is that, in C over pthreads.

## Tests

- `tests/chan.c`: order and growth, a producer thread (10 000 messages, in order, then disconnected),
  timeouts, bounded `try_send`, a closed receiver, and messages dropped at release.

## Licence

MIT. See the repository's LICENSE.
