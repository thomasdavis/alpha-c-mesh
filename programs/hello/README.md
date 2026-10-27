# @ajax/mesh-prog-hello

The smallest mesh GPU program, for lifecycle tests: op 0 returns `arg * 3 + 7`. View: `0x0` magic
"HELO" (set by init), `0x4` the last arg, `0x8` the last result. Counters: 0 tasks, 1 inits.

A `ptx` package: `sexc build` compiles `src/hello.c` for nvptx64; `mesh-build-programs` links it into
the catalog's `hello.ptx` in the cluster float mode.

- **Ported from:** alpha-rust-mesh `programs/hello/src/lib.rs`.
- **Tests:** on the CPU simulator, the same source compiled for the host: init, results (with wrapping),
  a bad op, a batch of 40 tickets over 2 nodes, the counters, `P_DONE`, `P_ONLINE`, a halt and a relaunch.
