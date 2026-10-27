# @ajax/mesh-source-hash

A native program's source identity, as the Rust build script's `source_hash`: every source file under
some folders, each fed as `path\0content` in byte order of the paths (`LC_ALL=C sort`), SHA-256, the first
16 hex digits. meshd reads it from the catalog's `source.hash` (every member that built the same tree
shares it, so a fleet can tell its images apart).

- **Ported from:** alpha-rust-mesh `scripts/build-programs.sh` (`source_hash`).
- **Differences:** the files are C sources: names ending `.c` or `.h`, `sexc.json` and `program.toml`
  (the Rust hashed `.rs`, `Cargo.toml`, `program.toml`, `.swift`, `.h`, `.m`); `.sexc` folders are
  skipped as `target` was. The folders are the program's, the worker SDK's (`mesh-worker*`), the
  contract's (`mesh-abi*`) and the references' (`mesh-ref*`), plus `# hash-also:`.
- **Tests:** a small tree hashed by hand gives the same digest; non-sources and `.sexc` are ignored; a
  change moves it.
