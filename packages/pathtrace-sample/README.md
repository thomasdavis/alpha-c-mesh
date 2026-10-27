# @ajax/pathtrace-sample

One camera sample's radiance (`pt_sample<suffix>`) and a pixel's pass (`pt_pixel_pass<suffix>`: samples
p·S … p·S + S − 1 summed in order from +0). A path bounces up to the scene's depth: diffuse surfaces take a
next-event light sample from the scene's quad light and a cosine-weighted bounce, metal reflects with fuzz,
glass reflects or refracts by Schlick's Fresnel, and Russian roulette ends paths from depth 3. NaN samples
count 0 and bright ones are clamped. A template header (`pathtrace/t_sample.h`), freestanding.

**Ported from** `crates/pathtrace-core/src/lib.rs` (`surface`, `onb`, `reflect`, `sample`, `clamp_sample`,
`pixel_pass`), operation for operation (including Rust's `-sign * n.x` as `(-sign) * n.x`). **Tested**
through 4000 camera samples and four rendered tiles per float type against the Rust.
