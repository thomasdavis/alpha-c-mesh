# @ajax/pathtrace-rng

The random numbers of programs/pathtrace: a counter-based hash (PCG's output function, Jarzynski & Olano
2020) of (seed, pixel, sample, dimension), so any sample is computable anywhere, in any order, on any
backend. `pt_sample_key(seed, pixel, sample)` then `pt_rnd(key, d)`: dimensions 0–1 jitter the pixel,
bounce b uses 2 + 8b … 2 + 8b + 7. Header only, freestanding.

**Ported from** `crates/pathtrace-core/src/lib.rs` (`pcg`, `sample_key`, `rnd`). **Tested** through every
camera sample of the renderer golden tests.
