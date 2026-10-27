# @ajax/pathtrace-tonemap

A linear sum of samples to an 8-bit sRGB pixel: divide by the sample count, apply the exposure, the ACES
filmic fit or Reinhard, then binary-search the sRGB table (`@ajax/pathtrace-srgb`) for the code;
`pt_tonemap<suffix>` returns 0xffBBGGRR. A template header (`pathtrace/t_tonemap.h`), freestanding.

**Ported from** `crates/pathtrace-core/src/lib.rs` (`curve`, `quantize`, `tonemap`). **Tested** with
200 000 inputs per float type against the Rust, and the Rust's `quantize_is_srgb`.
