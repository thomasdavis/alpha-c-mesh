# @ajax/mesh-ref-sha256d

programs/sha256d's CPU reference: SHA-256 written plainly from FIPS 180-4, double SHA-256, an 80-byte
block header hashed at a nonce and time, and Bitcoin's little-endian 256-bit targets (`zbits`,
`zbits_target`, `le_lt`, the reversed-hex display). The checkers compare the GPU miner against these.

**Ported from** `crates/mesh-ref/src/sha.rs`. Freestanding (no libc).

**Tested** with the FIPS 180-4 vectors (empty, "abc", the 448-bit message, 1000 × "a") and golden outputs
from the Rust for `sha256d`, `hash_at`, `display`, `zbits`, the targets at 0..256 bits and `le_lt`.

## Differences

- Rust returns `[u8; 32]` and `String`; here the caller passes the output buffers.
- `zbits_target` clamps z above 256 to 256 (the Rust would index out of bounds).
