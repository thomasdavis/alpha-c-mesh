# @ajax/pathtrace-digest

The digests of programs/pathtrace: FNV-1a 64 over u32 words (`pt_fnv`, `pt_fnv_words`), a tile's digest
folded from its 32 column digests (`pt_fold`), and a pass-sum payload's digest (`pt_payload_digest`, what
meshd's assembler checks a fleet job item against). Freestanding.

**Ported from** `crates/pathtrace-core/src/lib.rs` (`fnv`, `fold`, `payload_digest`, `FNV_*`) and
`crates/mesh-ref/src/pathtrace.rs` (`fnv_words`). **Tested** in the renderer golden tests, including the
Rust's check that a payload's digest equals its tile pass digest.
