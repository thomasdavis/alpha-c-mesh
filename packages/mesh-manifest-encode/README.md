# @ajax/mesh-manifest-encode

The bytes a manifest puts in the mailbox: the program-table entry (PT_*, with PT_STATE and PT_GEN left zero for the seqlock writer) and the manifest extension (PX_*: counter names, fields, place cap, param, shared and scratch bytes).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/manifest.rs (`Image`, `encode_entry`, `encode_ext`).

## Tests

`tests/golden.c`: 5696 manifests (`tests/data/manifests.in`) against what the Rust printed for each (`tests/data/manifests.rust`): the verdict and error message, the fnv1a64 of the manifest's `{:?}`, of the entry and of the extension, threads, shared memory and the place parts. One case per distinct Rust verdict out of 23396 generated.

## Licence

MIT. See the repository's LICENSE.
