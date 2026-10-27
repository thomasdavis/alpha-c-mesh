# @ajax/mesh-ptx-float-check

```
mesh-ptx-float-check PROGRAM.ptx
```

Refuses a program PTX that breaks the cluster float mode (README "Cluster float mode: FTZ" of
alpha-rust-mesh), so CUDA and Metal members of a cluster compute the same bits. Exit 0: fine; 1: refused,
with up to five `file:line: why: the line` problems on stderr; 2: usage. The rules are
@ajax/mesh-ptx-float-rules.

- **Ported from:** alpha-rust-mesh `scripts/ptx-float-check.sh`, same messages and exit codes.
- **Tests:** reading a file and reporting a fused multiply-add with its line; the rules have their own tests.
