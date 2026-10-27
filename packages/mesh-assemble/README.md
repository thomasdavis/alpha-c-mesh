# @ajax/mesh-assemble

The assembler of a fleet-rendered pathtrace image, sans IO. blobd feeds it the job's committed
results (each item's blob reference and pass-sum digest) and the blobs' bytes, and asks it what to
fetch next and for the image.

Item w = pass × tiles + tile is one tile's pass sums, computed from +0 wherever it ran. The image is
the reference's, bit for bit, because of three rules:

- **In pass order.** Each tile's pass sums are folded `acc = ((0 + P0) + P1) + ..`, every add in the
  cluster float mode Z.
- **Exactly once per (tile, pass).** A tile advances only by its next pass. A blob that arrives early
  waits, and the caller fetches at most `WINDOW` passes ahead. A second copy of a folded pass is
  dropped.
- **Checked first.** Every payload is checked against its item's pass-sum digest (the job record's
  `J_PART`) before it is folded. A wrong one is refused and kept in `bad`.

The 8-bit pixels are re-toned when a tile changes or the job's tone map does, so the image is
progressive and a tone-map swap never touches the accumulators. The assembler also gives the image's
digests (as kcheck prints them), raster, PPM, BMP (`@ajax/mesh-assemble-bmp`) and sha256.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT):
`crates/mesh-host/src/assemble.rs`: `WINDOW`, `Item`, `part_of`, `job_scene`, `renders`, `Assembly`.

## Tests

`tests/assemble.c`:
- The Rust's `ordered_fold_is_the_reference_exactly_once`. Five random orders of commits and blobs,
  with duplicates and early arrivals, fold bit for bit to the CPU reference
  (`@ajax/mesh-ref-pathtrace`): the accumulators, pixels and digests. A tone-map swap and back keeps
  the accumulators. A corrupted payload and a short one are refused.
- `default_reference_digests`: the default scene's shape, `job_scene`'s refusals, `renders`, and
  `part_of`.

## Licence

MIT. See the repository's LICENSE.
