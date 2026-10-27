# @ajax/mesh-blobs-frame

The blob frames on the peer links. Each is a type byte, a `u32` length, then a body that starts with
the blob's sha256:

| Frame | Body | Meaning |
|---|---|---|
| F_BLOB_PUT | hash, u64 job, u32 len, bytes | A push of the job's copy |
| F_BLOB_ACK | hash, u32 ok, u64 job | The receiver's copy is durable (or was refused) |
| F_BLOB_GET | hash, u64 job | A fetch: any job's copy serves |
| F_BLOB | hash, u32 found, bytes | A fetch's answer |

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/blobd.rs`: the frames `ack`, `tick` (pushes and gets) and `frame` (answers and
reading) write and read.

## Tests

`tests/frame.c`: each frame's bytes and lengths, read back, and short bodies refused.

## Licence

MIT. See the repository's LICENSE.
