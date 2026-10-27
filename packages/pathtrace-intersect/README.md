# @ajax/pathtrace-intersect

The nearest hit of a ray in (EPS, tmax) against a scene's quads, spheres and triangles, with the triangle
mesh skipped unless its bounding sphere is hit: `pt_intersect<suffix>(sc, &layout, o, d, tmax, &id)`
returns t and the hit id (0 none, 1 + i quad i, `PT_ID_SPH` + i, `PT_ID_TRI` + i). A template header
(`pathtrace/t_intersect.h`) over the float contract of `@ajax/pathtrace-math`. Freestanding.

**Ported from** `crates/pathtrace-core/src/lib.rs` (`intersect`), operation for operation. **Tested**
through 4000 camera samples and four rendered tiles per float type against the Rust.
