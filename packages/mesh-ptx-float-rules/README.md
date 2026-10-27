# @ajax/mesh-ptx-float-rules

The cluster float mode's rules on PTX text. Every float instruction must be correctly rounded, unfused
and exact, and every f32 instruction that has a `.ftz` form must carry it, so CUDA and Metal members of
a cluster compute the same bits:

| Instructions | Rule |
|---|---|
| `add sub mul div rcp sqrt` | `.rn`, and `.ftz` on f32 |
| `abs neg min max setp set slct` | `.ftz` on f32 |
| `cvt` | `.ftz` when the source is f32, or f64 to f32 |
| `selp copysign testp mov ld ldu st` | allowed as they are |
| `.approx`, `div.full`, `fma`/`mad`, f16/bf16/tf32/fp8, anything else with a float type | refused |

`mesh_ptx_float_line(line, len)` gives the reason a line breaks a rule (the script's words), and
`mesh_ptx_float_check(path, text, len, out)` checks a whole file, printing up to 5 problems.

- **Ported from:** alpha-rust-mesh `scripts/ptx-float-check.sh` (an awk program), rule for rule.
- **Tests:** every rule on hand-written lines; the programs' tests run it on every linked PTX, and the
  results agree with the original awk script on those files (checked by hand when porting).
