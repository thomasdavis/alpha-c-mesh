# @ajax/mesh-infer-wire

The gpt2 service protocol's wire: frames, key=value messages, checksummed KV layers.

```
@ajax/mesh-infer-wire: the wire of the inference services (programs/gpt2;
docs/plan-disagg-inference.md).

Every gpt2 worker listens on TCP; frames are a u32 length (LE, of what
follows), a u8 kind, a payload. A request (K_GEN, K_DECODE, K_PREFILL)
carries a Msg: `key=value` lines (sorted by key), a blank line, a body. KV
travels as K_LAYER frames (layer, rows, a checksum, the bytes); K_FIRST
ends a prefill; K_RESULT and K_ERROR answer.
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/infer.rs: `K_*`, `MAX_FRAME`, `REQUEST_WAIT`, `PROGRAM_*`, `write_frame`, `read_frame`, `read_frame_timed`, `Msg` (set, put, get, need, num, encode, decode), `layer_sum`, `layer_frame`, `parse_layer`.

## Tests

- `tests/wire.c`: infer.rs's `messages_and_layers_round_trip` (over a socketpair), and every error text.

## Differences

- A message holding a NUL byte is refused as not UTF-8 (C strings cannot carry it); Rust would accept it. The workers never send one.

## Licence

MIT. See the repository's LICENSE.
