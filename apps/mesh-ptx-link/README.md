# @ajax/mesh-ptx-link

```
mesh-ptx-link --mcpu sm_86 [-O0..3] [--debug] [--export SYM]... -o OUT.ptx IN.bc...
```

Links LLVM bitcode (a sexc `ptx` package's `.sexc/out/<profile>/ptx/<id>.bc`) into one PTX module in the
cluster float mode: `llvm-link`, then `opt --denormal-fp-math-f32=preserve-sign` with
`default<O3>,internalize,globaldce` keeping only `mesh_main` and `MESH_RT_ABI` public, then `llc` with the
same flag. Every f32 instruction that has a `.ftz` form gets it, and the rest of the program is internalised
and inlined into the kernel. Tools: `$MESH_LLVM_LINK`, `$MESH_OPT`, `$MESH_LLC`, or found on `PATH`
(also `name-21` .. `name-15`) or the newest `/usr/lib/llvm-N/bin`.

- **Ported from:** alpha-rust-mesh `scripts/ptx-link.sh` (rustc's linker for the programs).
- **Tests:** the exact commands, the symbols file, the `sm_NN` rule, tool lookup; the programs' tests link
  their PTX with it and float-check the result.
- **Differences:** a command-line tool instead of a rustc `-C linker`, so it takes the inputs and options
  directly. `llc` also gets `--fp-contract=off`: clang's C (unlike rustc) may emit `llvm.fmuladd` in a
  library built without `-ffp-contract=off`, and strict contraction keeps that a multiply and an add.
