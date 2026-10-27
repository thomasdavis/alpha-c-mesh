# @ajax/mesh-loader

The program catalog under `--programs DIR`: catalog names (`[a-z0-9-]`, never a path), resolving them inside the root (symlinks that escape are refused), reading a program's manifest and PTX, image hashes (fnv1a64 with `param` folded in), a native program's identity (manifest + `source.hash`), the catalog identity rule, the peer hash check and the catalog listing.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/loader.rs (`Payload`, `catalog_name`, `resolve`, `read`, `image_hash`, `identity`, `check_hash`, `catalog`) and native.rs (`identity`).

## Tests

`tests/loader.c`: the Rust's `names_only` and `confined_and_catalog`, reads with and without PTX, identities, hashes and the exact error messages.

## Licence

MIT. See the repository's LICENSE.
