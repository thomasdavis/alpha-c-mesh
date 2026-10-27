# @ajax/pathtrace-f32

programs/pathtrace's renderer with plain f32 operations: `pt_sample_f32`, `pt_tile_pass_f32`,
`pt_tile_fold_f32`, … (`pathtrace/declare.h`). On a CPU this is IEEE f32 with subnormals kept; a GPU
program instantiates the same templates and its build makes every operation `.ftz`.

**Ported from** pathtrace-core's `impl Real for f32`. Freestanding (builds for host, nvptx64 and wasm32).

**Tested** with golden digests from the Rust's f32 instance: four tiles of the default scene over two
passes (pass, fold, pixel and accumulator digests and a re-tone), 200 000 inputs through sqrt, cis,
quantize and tonemap, 4000 camera samples; plus the Rust's `sqrt_and_cis_are_close`, `quantize_is_srgb` and
the payload-digest check. It depends on the default scene for those tests (and every renderer starts
from it).
