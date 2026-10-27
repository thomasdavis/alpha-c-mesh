# @ajax/mesh-occupancy

Co-residency of resident CUDA kernels: every program's nodes must be on the GPU at once, forever, so a load is refused unless all of them fit (per kernel, the block budget, the occupancy shares, and whole-SM nodes against the spread of small blocks); and how many nodes of a shape still fit (`nodes = "fit"`).

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-host/src/backend.rs (`co_resident`, `room`).

## Tests

`tests/occupancy.c`: the Rust's `co_residency` test (RTX 3060 and GB10 shapes), with the exact messages.

## Licence

MIT. See the repository's LICENSE.
