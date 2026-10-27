# @ajax/mesh-ref-doom

programs/doom's CPU reference, operation for operation as the kernel computes in the cluster float mode:
`mesh_doom_cast` walks a column's ray through the 16×16 map by DDA and returns the wall kind, side and
perpendicular distance; `mesh_doom_height` is the column's wall height; `mesh_doom_map` is the map as the
view holds it; and the pose (position, direction, camera plane) moves under the keys `w s a d q e` —
the host pose of v1 with the view's turn constants, which the GPU-side pose must match.

**Ported from** `crates/mesh-ref/src/lib.rs` (`cast`, `height`, `doom_map`, `Pose`). Freestanding. The
constants and the map come from `@ajax/mesh-abi-view-doom`.

**Tested** by a golden digest from the Rust: the map, 2000 keys from the start pose (every pose, and all
128 columns' rays and heights every 50 keys), and 5000 odd rays from random spots in any direction
(including NaN and subnormal ones) with short step limits, plus heights and walkability.

## Differences

- Rust's `f32 as i32` saturates and maps NaN to 0; C's cast doesn't, so a helper does it the Rust way.
- `cast` returns the kind and writes the side and distance through pointers.
