# @ajax/mesh-reply

One-shot replies between threads: a requester makes a reply, hands a sender reference to the thread that
answers, and waits with a timeout for one `mesh_result` (Ok text, Ok number, Ok bytes, or an error). An
answerer that drops its reference unanswered is seen by the waiter at once; an answer that arrives after
the waiter gave up is freed with the channel.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/shared.rs`'s `Reply` (`Sender<Result<String, String>>`), `TaskReply`
(`Result<u32, String>`) and `HttpReply` (`Result<Vec<u8>, String>`): one C type for the three.

## Tests

- `tests/reply.c`: answered, dropped unanswered, answered from a thread, answered too late (no leak under
  ASan), values, bytes and clones.

## Licence

MIT. See the repository's LICENSE.
