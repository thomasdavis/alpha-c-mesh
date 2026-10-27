# @ajax/mesh-rt-view

`mesh_view`: a bounds-checked byte window of the mailbox (a program's view or a part of it), and
`mesh_rt_fault`, the first-fault record in the program's control block. An out-of-range access reads 0 or
drops the write and records `FAULT_VIEW_BOUNDS | off << 8` in `P_FAULT`.

| Rust (`View`) | C |
|---|---|
| `u32`, `f32`, `u64`, `u8` | `mesh_view_u32` ... `mesh_view_u8` |
| `set_u32` ... `set_u8` | `mesh_view_set_u32` ... `mesh_view_set_u8` |
| `sub(off, len) -> Option<View>` | `bool mesh_view_sub(v, off, len, &out)` |
| `len`, `is_empty`, `ptr` | `mesh_view_len`, `mesh_view_is_empty`, `mesh_view_ptr` |

- **Ported from:** alpha-rust-mesh `crates/mesh-rt/src/ctx.rs` (`View`, `fault`).
- **Tests:** through the programs' simulator tests (hello checks `P_FAULT` stays 0).
