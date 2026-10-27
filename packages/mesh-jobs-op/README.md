# @ajax/mesh-jobs-op

An edit of the job table, and the binary encoding of edits. Kinds: submit, cancel, done (with a
result), done-run (a range done with result 0), fail, fails (tries so far), dupes, finish, forget,
fold, and tone. An op also carries item results (`mj_res`) and payload references (`mj_blob_ref`:
sha256, length, holders). Encoded, an op is a `u8` tag and its fields (@ajax/mesh-jobs-wire). Ops
travel in parts of at most `PART_BYTES` (1 MiB) each; an op is never split.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/jobs.rs`: `Res`, `BlobRef`, `Op`, `put_op`, `get_op`, `encode_ops`,
`decode_ops`, `parts`, `PART_BYTES`.

## Tests

`tests/op.c`:
- one op of each kind, encoded byte for byte as the Rust encodes them (golden bytes from the Rust);
- decoding back, and deep clones;
- decode's answers to bad bytes, word for word the Rust's: an unknown tag, a fold of 9 members, a blob
  tag 2, a short read, a bad spec, bad UTF-8;
- parts of a 3 MiB op stream.

## Licence

MIT. See the repository's LICENSE.
