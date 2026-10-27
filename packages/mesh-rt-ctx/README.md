# @ajax/mesh-rt-ctx

`mesh_ctx`, what a mesh program's init and task see: ids (node, warp, lane, thread, leader), block
collectives, shared memory, scratch, the own view, peer views (gated by `reads`, read under the slot's
seqlock), the program table (`find`, `prog_state`, `prog_name`, `request_run`), the input queue, pipes
(`push_with`, `pop_with`, `pipe_free`, `pipe_len` over @ajax/mesh-abi-pipe with device atomics), the
request window and `respond`, the lockstep exchange page, counters, the ticket, the system and group.

| Rust | C |
|---|---|
| `ctx.view_u32(off)` | `mesh_ctx_view_u32(ctx, off)` |
| `ctx.peer_view(s) -> Option<View>` | `bool mesh_ctx_peer_view(ctx, s, &view)` |
| `ctx.find(b"train") -> Option<u32>` | `bool mesh_ctx_find(ctx, "train", 5, &slot)` |
| `ctx.input_pop() -> Option<u8>` | `bool mesh_ctx_input_pop(ctx, &byte)` |
| `ctx.push_with(s, \|v\| ..)` | `mesh_ctx_push_with(ctx, s, fill_fn, user)` |
| `ctx.shared::<f32>(off, len)` | `bool mesh_ctx_shared(ctx, off, len, 4, &shared)` |
| `ctx.for_strided(n, \|i\| ..)` | `MESH_CTX_FOR_STRIDED(ctx, n, i) { .. }` |
| `ctx.group() -> Option<(u32, u32)>` | `bool mesh_ctx_group(ctx, &rank, &n)` |

- **Ported from:** alpha-rust-mesh `crates/mesh-rt/src/ctx.rs`.
- **Tests:** through the programs' simulator tests (views, peer views, input, pipes, requests, counters).
