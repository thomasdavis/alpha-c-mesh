# Port: crates/mesh-ref and crates/pathtrace-core

alpha-rust-mesh's CPU references (`crates/mesh-ref`) and the path tracer's core (`crates/pathtrace-core`) are 31 C
packages under `packages/`. Everything here is checked against the Rust bit for bit: golden digests that a Rust
program computed from the crates themselves (at `00b526b`), plus every Rust unit test of the two crates, ported.

## How it is checked

- **Golden digests.** A throwaway Rust program (not committed) with path dependencies on copies of `mesh-ref`,
  `mesh-abi` and `pathtrace-core` ran each function over large input sets and printed FNV-1a 64 digests of every
  result's bits (and, for `Z`, the number of subnormals flushed). The C tests compute the same digests over the same
  inputs (the input generators are ported too) and compare. Inputs cover every float class: zeros, subnormals, the
  normal boundary, infinities, quiet and signalling NaNs with payloads, and values chosen so products and quotients
  cross the subnormal boundary.
- **The Rust's tests (14 of 15):** ftz.rs 2, block.rs 1, matmul.rs 1, gpt2.rs 5, pathtrace.rs 1, pathtrace-core 4 (all
  but `metal_port_agrees`, which checks the Metal port; Metal isn't ported). The gemm and sha references have
  no Rust tests; they are checked by goldens.
- **Every configuration.** `sexc test` (gcc, -O0, UBSan), `--release` (gcc -O2), `CC=clang` and `CC=clang --profile
  asan` all pass: 16 test programs here, bit-identical results in all four.
- **Device builds.** Every freestanding package compiles for `nvptx64-nvidia-cuda` (sm_86) and `wasm32`. A
  throwaway `kind: ptx` package depending on the renderer, cubic, doom, sha256d, block and the GPT-2 row operations
  built to PTX through sexc, and a `kind: wasm` package over `pathtrace-z` exported exactly the nine `mesh_ftz_*`
  functions meshd's float guard expects.

## Rust → C

| Rust | C package | C symbols |
|---|---|---|
| mesh-ref `ftz.rs`: `Z`, `flush`, `ops`, `flushes` | `@ajax/mesh-ftz` | `mesh_ftz_add/sub/mul/div/neg/abs/cmp/eq/ne/lt/le/gt/ge/to_f64/from_f64/from_u32/from_i32/is_nan/bits/from_bits/flush`, `mesh_ftz_flushes` |
| mesh-ref `block.rs` | `@ajax/mesh-ref-block` | `mesh_block_{warp_reduce,warp_scan,reduce,scan}_{f32,u32,u64}`, `mesh_block_max_f32`, `_add_*`, `_mix`, `_test_values` |
| mesh-ref `sha.rs` | `@ajax/mesh-ref-sha256d` | `mesh_sha256`, `mesh_sha256d`, `mesh_sha_hash_at`, `_le_lt`, `_zbits`, `_zbits_target`, `_display`, `_hex` |
| mesh-ref `matmul.rs` | `@ajax/mesh-ref-matmul` | `mesh_matmul_value`, `_matrix`, `mesh_matmul`, `mesh_matvec`, `mesh_matmul_digest` |
| mesh-ref `lib.rs` `gemm` | `@ajax/mesh-ref-gemm` | `mesh_gemm_a`, `_b`, `_inputs`, `_reference`, `_max_rel_err` |
| mesh-ref `lib.rs` `cubic_z`, `cubic`, `warp_sum`, `step_sums`, `apply`, `loss`, `sin_pi`, `grid`, `samples` | `@ajax/mesh-ref-cubic` | `mesh_cubic*` |
| mesh-ref `lib.rs` `cast`, `height`, `doom_map`, `Pose` | `@ajax/mesh-ref-doom` | `mesh_doom_cast`, `_height`, `_map`, `mesh_doom_pose_*`, `mesh_doom_walkable` |
| mesh-ref `gpt2.rs` constants, `BlockOff`, `Layout`, `tensors` | `@ajax/mesh-ref-gpt2-shape` | `MESH_GPT2_*`, `mesh_gpt2_layout_init`, `mesh_gpt2_tensor` |
| gpt2.rs `exp_poly`, `tanh_poly`, `gelu_tanh`, `sqrt_newton`, constants | `@ajax/mesh-ref-gpt2-scalar` | `mesh_gpt2_exp_poly`, … |
| gpt2.rs `Epi`, `project_row`, `project_cols` | `@ajax/mesh-ref-gpt2-linear` | `mesh_gpt2_project_row/cols` |
| gpt2.rs `row_reduce`, `normalize_row` | `@ajax/mesh-ref-gpt2-norm` | `mesh_gpt2_normalize_row` |
| gpt2.rs `attend_row` | `@ajax/mesh-ref-gpt2-attend` | `mesh_gpt2_attend_row` (pointer + stride for keys and values) |
| gpt2.rs `argmax`, `digest_extend`, `DIGEST_SEED` | `@ajax/mesh-ref-gpt2-logits` | `mesh_gpt2_argmax`, `mesh_gpt2_digest_extend` |
| gpt2.rs `synthetic` | `@ajax/mesh-ref-gpt2-weights` | `mesh_gpt2_synthetic` |
| gpt2.rs `Kv` | `@ajax/mesh-ref-gpt2-kv` | `mesh_gpt2_kv_*` |
| gpt2.rs `forward`, `forward_layers`, `generate`, `par_linear`, `par_rows`, `project_vocab` | `@ajax/mesh-ref-gpt2` | `mesh_gpt2_forward*`, `mesh_gpt2_generate`, `mesh_gpt2_project_vocab` |
| mesh-ref `pathtrace.rs` `Tile`, `tile`, `tiles`, `raster`, `ppm`; `impl Real for Z` | `@ajax/mesh-ref-pathtrace`; `@ajax/pathtrace-z` | `mesh_pt_tile_render`, `mesh_pt_tiles`, `mesh_pt_raster`, `mesh_pt_ppm`; `pt_*_z` |
| pathtrace-core scene constants, `Layout`, `layout`, `validate*`, `SETTABLE`, `set_arg`, `set_param`, `tiles*`, `tile_origin` | `@ajax/pathtrace-scene` | `PT_*`, `pt_layout_of`, `pt_validate*`, `pt_set_*`, `pt_tiles*`, `pt_tile_origin` |
| pathtrace-core `scene_data.rs` `DEFAULT_SCENE`; `SRGB_T` | `@ajax/pathtrace-default-scene`; `@ajax/pathtrace-srgb` | `pt_default_scene`; `pt_srgb_t` |
| pathtrace-core `view` | `@ajax/pathtrace-view` | `PT_V_*`, `PT_OP_*`, `pt_view_*` (header only) |
| pathtrace-core `job_scene` | `@ajax/pathtrace-job-scene` | `pt_job_scene` |
| pathtrace-core `pcg`, `sample_key`, `rnd` | `@ajax/pathtrace-rng` | `pt_pcg`, `pt_sample_key`, `pt_rnd` (header only) |
| pathtrace-core `fnv`, `fold`, `payload_digest`; mesh-ref `fnv_words` | `@ajax/pathtrace-digest` | `pt_fnv`, `pt_fold`, `pt_payload_digest`, `pt_fnv_words` |
| pathtrace-core `Real`, constants, `V` ops, `sqrt`, `cis` | `@ajax/pathtrace-math` | `pathtrace/real.h`, `consts.h`, `t_math.h` (templates) |
| pathtrace-core `intersect` | `@ajax/pathtrace-intersect` | `pathtrace/t_intersect.h` |
| pathtrace-core `surface`, `onb`, `reflect`, `sample`, `clamp_sample`, `pixel_pass` | `@ajax/pathtrace-sample` | `pathtrace/t_sample.h` |
| pathtrace-core `curve`, `quantize`, `tonemap` | `@ajax/pathtrace-tonemap` | `pathtrace/t_tonemap.h` |
| pathtrace-core `col_digest`, `column_pass`, `fold_column`, `retone`, `tile_*` | `@ajax/pathtrace-tile` | `pathtrace/t_tile.h`, `instantiate.h`, `declare.h` |
| pathtrace-core `impl Real for f32` | `@ajax/pathtrace-f32` | `pt_*_f32` |

## The renderer written once (Rust generics → C templates)

pathtrace-core is generic over `R: Real` and instantiated with `f32` (GPU) and `Z` (CPU, wasm). In C the renderer
lives in template headers (`pathtrace/t_*.h`) written over macros — `PT_SUFFIX`, `PT_ADD`, `PT_SUB`, `PT_MUL`,
`PT_DIV`, `PT_NEG`, `PT_LT`, `PT_GT`, `PT_LE`, `PT_GE` — with values always `float`. An instantiation defines them
and includes `pathtrace/instantiate.h`; every function becomes `pt_<name><suffix>`. `@ajax/pathtrace-f32` and
`@ajax/pathtrace-z` are the two instantiations of the Rust; a GPU program instantiates the same headers with native
operations (its build makes them `.ftz`), e.g. `PT_SUFFIX _dev` with `PT_API static inline`.

## Package graph

```
mesh-ftz ─┬─ mesh-ref-block ─┬─ mesh-ref-matmul ── mesh-ref-gpt2-weights ─┐
          │                  ├─ mesh-ref-cubic (+ mesh-abi-view-train)     │
          │                  ├─ mesh-ref-gpt2-norm ─────────────────────── mesh-ref-gpt2
          │                  └─ mesh-ref-gpt2-attend ───────────────────────┤
          ├─ mesh-ref-gpt2-scalar ─┬─ mesh-ref-gpt2-linear ─────────────────┤
          │                        └─ (norm, attend)                         │
          ├─ mesh-ref-gpt2-logits ── mesh-ref-gpt2-kv ──────────────────────┘
          ├─ mesh-ref-doom (+ mesh-abi-view-doom)
          └─ pathtrace-z ── mesh-ref-pathtrace
mesh-ref-gpt2-shape → norm, attend, kv, weights, gpt2
mesh-ref-sha256d, mesh-ref-gemm: standalone

pathtrace-scene ─┬─ pathtrace-view ── pathtrace-job-scene (+ pathtrace-default-scene)
                 ├─ pathtrace-digest
                 └─ pathtrace-math ─ pathtrace-intersect ─ pathtrace-sample (+ pathtrace-rng)
                                   └ pathtrace-tonemap (+ pathtrace-srgb)
                    all of them ── pathtrace-tile ─┬─ pathtrace-f32
                                                   └─ pathtrace-z (+ mesh-ftz)
```

## Where C can't simply copy the Rust

- **NaN payloads of two NaN operands** (`mesh-ftz`): Rust leaves the operand order of a commutative `+`/`*` to the
  compiler, and x86 returns the first instruction operand's NaN, so the Rust's payload depends on how it compiled.
  The C makes it explicit: `a`'s NaN, else `b`'s, quieted — what the Rust produced in every case measured (120 of
  120 NaN pairs). Without this, gcc -O0 and -O2 gave different payloads.
- **`f32 as i32`** saturates and maps NaN to 0 in Rust; C's cast is undefined there, so a helper does the Rust's
  thing (`mesh-ref-doom`, `mesh-ref-gpt2-scalar`).
- **`validate`'s tile count** is computed in 64 bits; a Rust release build would wrap on an absurd width × height.
- **Panics** (wrong block sizes, context overflow, out of memory) become return values.
- **Performance**: `mesh-ref-gpt2-linear`, `-attend` and `mesh-ref-matmul` are compiled `-O2` in every profile
  (a forward pass is billions of operations); the model test takes ~17 s in a debug build.
- Everything that computes in `Z` must be compiled with `-ffp-contract=off` (each package says so in its cflags).
