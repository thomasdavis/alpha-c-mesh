# @ajax/sha256-stream

SHA-256 over a stream of bytes: `sha256_stream_init`, `_update`, `_final`, and `sha256_stream_hex`.
Built on @ajax/sha256-compress (one compression and H(0)); freestanding.

- **Ported from:** nothing directly. The Rust used `sha256sum` (in `scripts/build-programs.sh`'s
  `source_hash`) and the `sha2` crate; mesh-build-programs needs a streaming SHA-256 in C.
- **Tests:** the FIPS 180-4 / NIST vectors (empty, "abc", the two-block message split every way from 1
  to 64 bytes, a million "a"s).
